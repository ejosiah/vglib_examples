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
    vec3 cameraPosition;
    float cloudMinHeight;
    float cloudMaxHeight;
    float coverage;
    float cloudType;
    float precipitation;
    float eccentricity;
    float scale;
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

float sampleCloudDensity(vec3 p, float height_fraction, float cloud_type, float cloud_coverage) {
    vec3 sp = p/u.scale;
    vec4 noiseComp = texture(lowFreqencyNoises, sp);
    float perlinWorly = noiseComp.x;
    float wfbm = dot(vec3(.625, .25, .125), noiseComp.gba);
    float density = remap(perlinWorly, wfbm - 1, 1, 0, 1);
    float densityHeightField = densityHeightGradientForPoint(p, height_fraction, cloud_type);
    density *= densityHeightField;

    density = remap(density, 1 - cloud_coverage, 1, 0, 1);
    density *= cloud_coverage;

    vec3 highNoise = texture(highFreqencyNoises, p * 0.1).rgb;
    float highFreqencyFBM = dot(highNoise, vec3(.625, .25, .125));
    float highFreqencyNoiseModifier = mix(highFreqencyFBM, 1 - highFreqencyFBM, clamp(height_fraction * 10, 0, 1));
    density = remap(density, highFreqencyNoiseModifier * 0.2, 1.0, 0.0, 1.0);

    return density;
}

float getHeightFraction(vec3 p) {
    float cMin = atmosphere.bottom_radius + u.cloudMinHeight;
    float cMax = atmosphere.bottom_radius + u.cloudMaxHeight;
    float x = length(p);

    return (x - cMin)/(cMax - cMin);
}

void main() {
    vec3 earth = vec3(0, atmosphere.bottom_radius, 0);
    vec3 cameraPos = localUnitsToAtmosphere(u.cameraPosition) + earth;
    vec3 direction = normalize(fs_in.viewDirection);
    vec3 color = vec3(0);

    float tMin, tMax;
    float density = 0;
    fragColor = vec4(0);
    bool hit = false;
    if(intersectsCloudShell(cameraPos, direction, tMin, tMax, color)) {
        hit = true;
        float depth = getWorldDepth(fs_in.uv);

        if(depth > tMin) {
            const float minSteps = 64;
            const float maxSteps = 256;
//            const float numSteps = mix(maxSteps, minSteps, direction.y);
            const float numSteps = maxSteps;
            const float stepStride = 1;
            const float offset = 0.5;
            vec3 origin = cameraPos + direction * tMin;
            const float ct = u.cloudType;
            const float cc = u.coverage;
            float limit = tMax - tMin;
            float dt = limit/numSteps;

//            vec4 clipPos = u.viewProjection * vec4(origin, 1);
//            clipPos /= clipPos.w;
//            gl_FragDepth = clipPos.z;
//
//            ivec2 fc = ivec2(gl_FragCoord);
//            if(u.mouse.z == 1 && u.mouse.x == fc.x && u.mouse.y == fc.y) {
//                vec3 o = origin;
//                debugPrintfEXT("o: [%f, %f, %f], t: [%f, %f], depth: %f, clipZ: %f\n", o.x, o.y, o.z, tMin, tMax, depth, clipPos.z);
//            }

            bool firstHit = true;
            for(int i = 0; i < numSteps; i++) {
                float t = (i + 0.3) * dt;
                vec3 sp = origin + direction * t;
                float h = getHeightFraction(sp);
                float density = sampleCloudDensity(sp, h, ct, cc);
                float pa = clamp(density - (density * fragColor.a), 0, 1);
                fragColor.rgb = pa * vec3(density) + fragColor.rgb;
                fragColor.a += pa;

                if(firstHit && density > 0) {
                    vec4 clipPos = u.viewProjection * vec4(sp, 1);
                    clipPos /= clipPos.w;
                    gl_FragDepth = clipPos.z;

                }

                if(fragColor.a > 0.999) break;
            }
        }
    }
}