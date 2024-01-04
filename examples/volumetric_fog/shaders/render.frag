#version 460 core
#extension GL_EXT_scalar_block_layout : enable

#define RADIANCE_API_ENABLED
#define SCENE_SET 3
#define SHADOW_MAP_SET 5

#include "atmosphere_api.glsl"
#include "scene.glsl"
#include "shadow_map.glsl"

const vec3 globalAmbient = vec3(0.2);

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

void main(){
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb;
    vec3 point = fs_in.position;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(point - scene.earthCenter, N, scene.sunDirection, skyIrradiance);
    vec3 transmittance;
    vec3 in_scatter = GetSkyRadianceToPoint(scene.camera - scene.earthCenter, point - scene.earthCenter, 0, scene.sunDirection, transmittance);

    vec3 diffuse = (albedo / PI) * sunIrradiance;
    vec3 radiance =  diffuse * transmittance + in_scatter;

    float shadow = pcfFilteredShadow(fs_in.lightSpacePos);
    radiance *= 1 - shadow;
    radiance += globalAmbient * albedo * skyIrradiance;

    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
//    fragColor.rgb = albedo;
}