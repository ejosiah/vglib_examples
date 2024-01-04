#version 460 core
#extension GL_EXT_scalar_block_layout : enable

#define RADIANCE_API_ENABLED
#define SCENE_SET 3
#define SHADOW_MAP_SET 5
#define FOG_SET 6

#include "atmosphere_api.glsl"
#include "scene.glsl"
#include "shadow_map.glsl"
#include "fog.glsl"

const vec3 globalAmbient = vec3(0.05);

layout(constant_id = 0) const uint materialOffset = 192;

layout(set = 2, binding = 0) uniform MATERIAL {
    vec3 diffuse;
    vec3 ambient;
    vec3 specular;
    vec3 emission;
    vec3 transmittance;
    float shininess;
    float ior;
    float opacity;
    float illum;
};

layout(set = 2, binding = 1) uniform sampler2D ambientMap;
layout(set = 2, binding = 2) uniform sampler2D diffuseMap;
layout(set = 2, binding = 3) uniform sampler2D specularMap;
layout(set = 2, binding = 4) uniform sampler2D normalMap;
layout(set = 2, binding = 5) uniform sampler2D ambientOcclusionMap;

layout(set = 4, binding = 0) uniform LightSpaceMatrix {
    mat4 lightViewProjection;
};



layout(location = 0) in struct {
    vec4 lightSpacePos;
    vec3 position;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

float saturate(float x){
    return max(0, x);
}

vec4 constantFog() {
    float density = fog.constantDensity;
    return scatteringExtinctionFromColorDensity(FOG_COLOR, density);
}

vec4 heightFog(vec3 pos) {
    float h = max(0, pos.y);
    float density = fog.heightFogDensity * exp(-fog.heightFogFalloff * h);
    return scatteringExtinctionFromColorDensity(FOG_COLOR, density);
}

float compute(vec3 pos, float distance, out vec3 scattering, out float transmittance) {
    vec4 scatteringAndExtinction = constantFog() + heightFog(pos);
    scattering = scatteringAndExtinction.rgb;

    float extinction = scatteringAndExtinction.a;
    transmittance = exp(-extinction * distance);

    return extinction;
}

float computeInScattering(vec3 pos, inout vec3 totalScattering) {
    const vec3 sunLight = ATMOSPHERE.solar_irradiance/PI;
    const vec3 wi = normalize(pos - scene.camera);
    const int maxSteps = 50;
    float currentStep = 0;
    float maxDistance = length(pos - scene.camera);
    const int stepCount = maxSteps;
    float totalDistance = 0;
    float totalTransmittance = 1;
    float stepDelta = maxDistance/float(maxSteps);

    float d = stepDelta;
    for(int i = 0; i < stepCount; i++) {
        if(totalDistance > maxDistance) return totalTransmittance;

        vec3 lPos = scene.camera + wi * d;
        vec3 scattering;
        float transmittance;
        float extinction = compute(lPos, stepDelta, scattering, transmittance);
        scattering = (scattering - (scattering * transmittance))/ max(extinction, 0.00001);

        vec4 worldSpacePos = vec4(lPos, 1);
//        vec4 lightSpacePos = scene.lightViewProjection * worldSpacePos;
        vec4 lightSpacePos = lightViewProjection * worldSpacePos;
        vec3 L = normalize(scene.sunDirection);
        vec3 V = normalize(scene.camera - lPos);
        float visibility = 1 - shadowCalculation(lightSpacePos);
        float phaseFn = phaseHG(dot(-L, V), fog.g);
        totalScattering +=  scattering * totalTransmittance * visibility * phaseFn * sunLight;

        totalTransmittance *= transmittance;
        totalDistance = d;
        d += stepDelta;
    }

    return totalTransmittance;
}

void main(){
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb;
    vec3 point = fs_in.position;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(point - scene.earthCenter, N, scene.sunDirection, skyIrradiance);

    vec3 inScattering = vec3(0);
    float transmittance = computeInScattering(point, inScattering);

    vec3 ambient = globalAmbient * albedo;
    vec3 diffuse =  (albedo / PI) * sunIrradiance;

    vec3 radiance = ambient + diffuse * (1 - shadowCalculation(fs_in.lightSpacePos));

    radiance = radiance * transmittance + inScattering;

    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
}