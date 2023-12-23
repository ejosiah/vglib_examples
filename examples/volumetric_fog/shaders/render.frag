#version 460 core
#extension GL_EXT_scalar_block_layout : enable

#define RADIANCE_API_ENABLED
#include "atmosphere_api.glsl"

const vec3 globalAmbient = vec3(0.1);
const vec3 whitePoint = vec3(1);

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

layout(set = 3, binding = 0) buffer SCENE_INFO {
    vec3 sunDirection;
    vec3 sunSize;
    vec3 earthCenter;
    vec3 camera;
    float exposure;
};

layout(set = 5, binding = 0) uniform sampler2D shadowMap;

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

float shadowCalculation(vec4 lightSpacePos){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float closestDepth = texture(shadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;
    float shadow = currentDepth > closestDepth ? 1.0 : 0.0;
    return shadow;
}

float pcfFilteredShadow(vec4 lightSpacePos){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float shadow = 0.0f;
    float currentDepth = projCoords.z;
    vec2 texelSize = 1.0/textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; x++){
        for(int y = -1; y <= 1; y++){
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

void main(){
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb;
    vec3 point = fs_in.position;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(point - earthCenter, N, sunDirection, skyIrradiance);
    vec3 transmittance;
    vec3 in_scatter = GetSkyRadianceToPoint(camera - earthCenter, point - earthCenter, 0, sunDirection, transmittance);

    vec3 radiance =  (albedo / PI) * sunIrradiance * transmittance + in_scatter;

    float shadow = pcfFilteredShadow(fs_in.lightSpacePos);
    radiance *= 1 - shadow;
    radiance += globalAmbient * albedo * skyIrradiance;

    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / whitePoint * exposure), vec3(1.0 / 2.2));
//    fragColor.rgb = albedo;
}