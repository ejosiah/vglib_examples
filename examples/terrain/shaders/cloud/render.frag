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
#define weatherTexture global_textures[nonuniformEXT(u.weatherTextureIndex)]

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
    float sigmaS;
    float scale;
    float hScale;
    float time;
    uint detailedSamples;
    uint useWeatherTexture;
    uint maxSteps;
    uint lowFrequencyNoisesIndex;
    uint highFrequencyNoisesIndex;
    uint weatherTextureIndex;
} u;

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 3, binding = 1, input_attachment_index = 1) uniform subpassInput positionInput;

struct Weather {
    float cloudCoverage;
    float cloudType;
    float precipitation;
};

AtmosphereParameters atmosphere = GetAtmosphereParameters();
float sigmaE;
Weather weather;

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

float sampleCloudDensity(vec3 p, float height_fraction, float lod, bool detailed) {
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
    float densityHeightField = densityHeightGradientForPoint(p, height_fraction, weather.cloudType);
    density *= densityHeightField;

    density = remap(density, 1 - weather.cloudCoverage, 1, 0, 1);
    density *= weather.cloudCoverage;

    if(density <= 0) return 0.0;
    if(!detailed) return density;

    vec3 highNoise = textureLod(highFreqencyNoises, p/u.hScale, lod).rgb;
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

const vec3 noise_kernel[] = {
    vec3(-0.316253, 0.147451, -0.902035),
    vec3(0.208214, 0.114857, -0.669561),
    vec3(-0.398435, -0.105541, -0.722259),
    vec3(0.0849315, -0.644174, 0.0471824),
    vec3(0.470606, 0.99835, 0.498875),
    vec3(-0.207847, -0.176372, -0.847792)
};

float henyeyGreenstein(float LdotV, float g){
    g = clamp(g, -0.99, 0.99);
    float _2gcos0 = 2 * g * LdotV;
    float gg = g * g;
    float num = 1 - gg;
    float denum = 4 * PI * pow(1 + gg - _2gcos0, 1.5);

    return num / denum;
}

float lightEnergy(float sampleDensity, float cloudDensity){
    return 2.0 * exp(-sampleDensity * sigmaE) * (1 - exp(-2 * cloudDensity * sigmaE));
}

float sampleCloudDensityAlongCone(vec3 samplePos, vec3 direction){
    vec3 lightStep = direction * 0.001;
    float coneSpreadMultiplier = length(lightStep);

    float density = 0;
    vec3 p = samplePos;
    float lod = 0;
    for(int i = 0; i < 6; i++){
        p += lightStep * (coneSpreadMultiplier * noise_kernel[i] * float(i));
        float h = getHeightFraction(p);
        // TODO generate lod
        // lod = float(i);
        density += sampleCloudDensity(p, h, lod, false);
    }

    return density;
}

float sampleLightEnergy(vec3 samplePosition, vec3 lightDirection, float cloudDensity, float dt){
    if(cloudDensity <= 0) return 1;

    float sampleDensity = sampleCloudDensityAlongCone(samplePosition, lightDirection) * dt;
    return lightEnergy(sampleDensity, cloudDensity);

}

vec3 GetTransmisstanceToSun(AtmosphereParameters atmosphere, sampler2D transmittanceTexture, vec3 position, vec3 sunDirection) {
    float viewHeight = length(position);
    const vec3 UpVector = position / viewHeight;
    float viewZenithCosAngle = dot(sunDirection, UpVector);
    vec2 uv;
    LutTransmittanceParamsToUv(atmosphere, viewHeight, viewZenithCosAngle, uv);
    return texture(transmittanceTexture, uv).rgb;
}


vec4 rayTrace(vec3 origin, vec3 direction, float LdotV, float rayLength, float numSteps) {
    float dt = rayLength/numSteps;
    float lod = 0.0;
    const float sunIntensity = 100;

    float density = 0;
    vec3 inScattering = vec3(0);
    float transmission = 1;
    for(int i = 0; i < numSteps; i++) {
        float t = (i + 0.3) * dt;
        vec3 sp = origin + direction * t;
        float h = getHeightFraction(sp);

        float density = sampleCloudDensity(sp, h, lod, u.detailedSamples == 1);

        float stepTransmission = exp(-density * dt * sigmaE);
        transmission *= stepTransmission;

        vec3 lightRayAttenuation = vec3(1);
        if(density > 0) {
            float attenuation = sampleLightEnergy(sp, atm.sunDirection, density * dt, dt);
            if(attenuation > 0) {
                vec3 sunTransmission = GetTransmisstanceToSun(atmosphere, transmittanceLUT, sp, atm.sunDirection);
                lightRayAttenuation *= sunTransmission;
            }
            lightRayAttenuation *= attenuation;
            float phase = henyeyGreenstein(LdotV, u.eccentricity);
            inScattering += sunIntensity * transmission * phase * lightRayAttenuation * u.sigmaS * density;
        }


        if(transmission < 1e-3) break;
    }

    return vec4(inScattering, transmission);
}

void main() {
    fragColor = vec4(0, 0, 0, 1);
    vec3 earth = vec3(0, atmosphere.bottom_radius, 0);
    vec3 cameraPos = localUnitsToAtmosphere(u.cameraPosition) + earth;
    vec3 direction = normalize(fs_in.viewDirection);
    vec3 color = vec3(0);

    weather = Weather(u.coverage, u.cloudType, u.precipitation);

    float tMin, tMax;
    bool hit = intersectsCloudShell(cameraPos, direction, tMin, tMax, color);
    if(!hit) return;

    float depth = getWorldDepth(fs_in.uv);
    if(depth < tMin) return;

    const float minSteps = 64;
    const float maxSteps = float(u.maxSteps);

//    const float numSteps = mix(maxSteps, minSteps, direction.y);
    const float numSteps = maxSteps;  // TODO rather than taking more steps at the horizon take more steps closer to the viewer
    vec3 origin = cameraPos + direction * tMin;

    if(u.useWeatherTexture == 1) {
        vec2 uv = .5 + .5 * (origin.xz / vec2(52.660));
        vec3 w = texture(weatherTexture, uv).rgb;
        weather.cloudCoverage = w.r;
        weather.cloudType = w.g;
    }

    if(weather.cloudCoverage == 0) return;

    sigmaE = weather.precipitation + u.sigmaS;

    float LdotV = dot(direction, atm.sunDirection);
    vec4 result = rayTrace(origin, direction, LdotV, tMax - tMin, numSteps);

    fragColor = vec4(result.rgb, result.a);
}