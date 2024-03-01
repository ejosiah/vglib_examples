#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
//#extension GL_EXT_ray_tracing_position_fetch : requre

#include "rt_constants.glsl"
#include "sampling.glsl"
#include "ray_tracing_lang.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "random.glsl"
#include "util.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform Uniforms {
    mat4 viewInverse;
    mat4 projInverse;
    uint mask;
};

layout(set = 5, binding = 3, rgba32f) uniform image2D indirectRadiance;

layout(set = 3, binding = 0) buffer LIGHT {
    Light light;
};

layout(location = 0) rayPayloadIn RtParams rtParams;

layout(location = 1) rayPayload bool isVisible;

hitAttribute vec2 attribs;

uint numSamples = 100;
uint numShadowSamples = 50;

vec3 computeRadiance(HitData hit, vec3 lightPos) {
    vec3 eyes = gl_WorldRayOrigin;
    vec3 lightDir = lightPos - hit.position;
    vec3 L = normalize(lightDir);

    vec3 N = normalize(hit.normal);
    vec3 E = normalize(eyes - hit.position);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    const float pi = 3.1415;
    const float dist = length(lightDir);
    vec3 radiance = max(0, dot(light.normal, -L)) * light.power /(dist * dist * pi);
    radiance;

    return hit.material.emission + radiance * max(0, dot(L, N)) * hit.material.diffuse;
}

float computeVisiblity(HitData hit) {

    const uint rayFlags =  gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHit | gl_RayFlagsSkipClosestHitShader;
    vec3 origin = offsetRay(hit.position, hit.normal);
    vec3 dim = light.upperCorner - light.lowerCorner;

    float visibility = 0;
    uint N = numShadowSamples;
    mat3 TBN = mat3(light.tangent, light.bitangent, light.normal);
    for(uint i = 0; i < N; i++) {
        isVisible = false;
        vec2 point = hammersley(i, N);
//        vec2 point = vec2(rand(rtParams.rngState), rand(rtParams.rngState));
        vec3 lightPos = light.lowerCorner + dim * (TBN * vec3(point, 0));

        vec3 direction = lightPos - origin;
        const float tMax = length(direction);
        direction = normalize(direction);

        traceRay(topLevelAS, rayFlags, mask & ~OBJ_MASK_LIGHTS, 0, 0, SHADOW_MISS, origin, 0, direction, tMax, 1);
        visibility += isVisible ? 1 : 0;
    }

    return visibility / float(N);
}

void main() {
    HitData hit = getHitData(attribs);

    vec3 color = vec3(0);
    vec3 dim = light.upperCorner - light.lowerCorner;
    for(uint i = 0; i < numSamples; i++){
        vec2 point = hammersley(i, numSamples);
        vec3 lightPos = light.lowerCorner + dim * vec3(point.x, 0, point.y);
        color += computeRadiance(hit, lightPos);
    }
    color /= float(numSamples);

    color *= computeVisiblity(hit);
    color += imageLoad(indirectRadiance, rtParams.pixelId).rgb;

    color /= (color + 1);
    rtParams.color = color;
    rtParams.position = hit.position;
    rtParams.normal = hit.normal;
    rtParams.material =  vec4(hit.material.diffuse, 1);
    rtParams.objectType = OBJECT_TYPE_DIFFUSE;
}