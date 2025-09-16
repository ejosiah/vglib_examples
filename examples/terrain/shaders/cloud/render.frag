#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_debug_printf : enable

#define ATMOSPHERE_UNIFORM_SET 2
#include "../atmosphere/atm_uniforms.glsl"
#include "../atmosphere/common_func.glsl"
#include "raytracing_implicits/implicits.glsl"

#define lowFreqencyNoises global_textures_3d[nonuniformEXT(u.lowFrequencyNoisesIndex)]
#define highFreqencyNoises global_textures_3d[nonuniformEXT(u.highFrequencyNoisesIndex)]

layout(set = 0, binding = 0, scalar) uniform Uniforms {
    mat4 viewProjection;
    ivec4 mouse;
    vec3 windDirection;
    float windSpeed;
    vec3 cameraPosition;
    float cloudTopOffset;
    float cloudMinHeight;
    float cloudMaxHeight;
    float coverage;
    float cloudType;
    float precipitation;
    float eccentricity;
    float scale;
    float time;
    uint detailedSamples;
    uint maxSteps;
    uint lowFrequencyNoisesIndex;
    uint highFrequencyNoisesIndex;
} u;

layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 3, binding = 1, input_attachment_index = 1) uniform subpassInput positionInput;


AtmosphereParameters atmosphere = GetAtmosphereParameters();

bool intersectsCloudShell(vec3 origin, vec3 direction, out float tMin, out float tMax, out vec3 color) {
    Ray ray = Ray(origin, direction);
    Sphere s0 = Sphere(vec3(0), atmosphere.bottom_radius + u.cloudMinHeight);
    Sphere s1 = Sphere(vec3(0), atmosphere.bottom_radius + u.cloudMaxHeight);

    vec2 t0, t1;
    bool hitLowerHull = sphere_ray_test(s0, ray, t0);
    bool hitUpperHull = sphere_ray_test(s1, ray, t1);

    if(!hitLowerHull && !hitUpperHull) return false;

    if(hitLowerHull && hitUpperHull) {
        if(t0.x == 0 && t1.x == 0) { // we are below the clouds
            tMin = t0.y;
            tMax = t1.y;
            color = vec3(0, 1, 0);
        }else { // we are inside / above the clouds
            tMin = t1.x;
            tMax = t0.x;
            color = t1.x == 0 ? vec3(0, 1, 0) : vec3(1, 0, 0);
        }
    }else if (hitLowerHull) {
        tMin = t0.x;
        tMax = t0.y;
        color = vec3(0, 1, 0);
    }else if (hitUpperHull) {
        tMin = t1.x;
        tMax = t1.y;
        color = vec3(1, 0, 0);
    }
//    debugPrintfEXT("lowerHull: [%d, %f, %f], upperHull [%d, %f, %f], minMax: [%f, %f], \n",int(hitLowerHull), t0.x, t0.y, int(hitUpperHull), t1.x, t1.y, tMin, tMax);
    return true;
}

layout(location = 0) in struct {
    vec3 viewDirection;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

float getWorldDepth(vec2 uv) {
    float d = subpassLoad(positionInput).w;
    vec4 clipPos = vec4(fs_in.uv * 2 - 1, d, 1);
    vec4 viewPos = atm.inverseProjection * clipPos;
    viewPos /= viewPos.w;
    return localUnitsToAtmosphere(length(viewPos));
}

float densityHeightGradientForPoint(vec3 p, float height, float cloud_type){

    const vec4 stratusGrad = vec4(0.02f, 0.05f, 0.09f, 0.11f);
    const vec4 stratocumulusGrad = vec4(0.02f, 0.2f, 0.48f, 0.625f);
    const vec4 cumulusGrad = vec4(0.01f, 0.0625f, 0.78f, 1.0f);
    float stratus = 1.0f - clamp(cloud_type * 2.0f, 0, 1);
    float stratocumulus = 1.0f - abs(cloud_type - 0.5f) * 2.0f;
    float cumulus = clamp(cloud_type - 0.5f, 0, 1) * 2.0f;
    vec4 cloudGradient = stratusGrad * stratus + stratocumulusGrad * stratocumulus + cumulusGrad * cumulus;
    return smoothstep(cloudGradient.x, cloudGradient.y, height) - smoothstep(cloudGradient.z, cloudGradient.w, height);
}

float sampleCloudDensity(vec3 p, float height_fraction, float cloud_type, float cloud_coverage, float lod, bool detailed) {
    float windSpeed = u.windSpeed;
    float cloudTopOffset = u.cloudTopOffset;

    vec3 pp = p;
    p += height_fraction * u.windDirection * u.cloudTopOffset;
    p += (u.windDirection + vec3(0, 1, 0)) * u.time * windSpeed;

    vec3 sp = p/u.scale;

    vec4 noiseComp = textureLod(lowFreqencyNoises, sp, lod);
    float perlinWorly = noiseComp.x;
    float wfbm = dot(vec3(.625, .25, .125), noiseComp.gba);
    float density = remap(perlinWorly, wfbm - 1, 1, 0, 1);
    float densityHeightField = densityHeightGradientForPoint(p, height_fraction, cloud_type);
    density *= densityHeightField;

    density = remap(density, 1 - cloud_coverage, 1, 0, 1);
    density *= cloud_coverage;

    if(density <= 0) return 0.0;
    if(!detailed) return density;

    vec3 highNoise = textureLod(highFreqencyNoises, p * 0.5, lod).rgb;
    float highFreqencyFBM = dot(highNoise, vec3(.625, .25, .125));
    float highFreqencyNoiseModifier = mix(highFreqencyFBM, 1 - highFreqencyFBM, clamp(height_fraction * 10, 0, 1));
    density = remap(density, highFreqencyNoiseModifier * 0.2, 1.0, 0.0, 1.0);

    return clamp(density, 0, 1);
}

float getHeightFraction(vec3 p) {
    float cMin = atmosphere.bottom_radius + u.cloudMinHeight;
    float cMax = atmosphere.bottom_radius + u.cloudMaxHeight;
    float x = length(p);

    return (x - cMin)/(cMax - cMin);
}

vec4 rayTrace(vec3 origin, vec3 direction, float rayLength, float numSteps) {
    const float ct = u.cloudType;
    const float cc = u.coverage;
    float dt = rayLength/numSteps;
    float lod = 0.0;

    float density = 0;
    vec4 result = vec4(0);
    for(int i = 0; i < numSteps; i++) {
        float t = (i + 0.3) * dt;
        vec3 sp = origin + direction * t;
        float h = getHeightFraction(sp);

        float density = sampleCloudDensity(sp, h, ct, cc, lod, u.detailedSamples == 1);
        float pa = clamp(density - (density * result.a), 0, 1);
        result.rgb = pa * vec3(density) + result.rgb;
        result.a += pa;

        if(result.a > 0.999) break;
    }

    return result;
}

void main() {
    fragColor = vec4(0);

    vec3 earth = vec3(0, atmosphere.bottom_radius, 0);
    vec3 cameraPos = localUnitsToAtmosphere(u.cameraPosition) + earth;
    vec3 direction = normalize(fs_in.viewDirection);
    vec3 color = vec3(0);


    float tMin, tMax;
    bool hit = intersectsCloudShell(cameraPos, direction, tMin, tMax, color);
    if(!hit) return;

    float depth = getWorldDepth(fs_in.uv);
    if(depth < tMin) return;

    const float minSteps = 64;
    const float maxSteps = float(u.maxSteps);

    // TODO up direction is not always going to be vec3(0, 1, 0) use dot(camDirection, updirection)
    const float numSteps = mix(maxSteps, minSteps, direction.y);
    vec3 origin = cameraPos + direction * tMin;

    vec4 result = rayTrace(origin, direction, tMax - tMin, numSteps);
    fragColor = vec4(result.rgb * 10, result.a);
}