#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "rt_constants.glsl"
#include "ray_tracing_lang.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "util.glsl"

layout(binding = 0, set = 0) uniform accelerationStructureEXT topLevelAS;

layout(set = 0, binding = 1) uniform Uniforms {
    mat4 viewInverse;
    mat4 projInverse;
    uint mask;
};

layout(set = 3, binding = 0) buffer LIGHT {
    vec3 position;
    vec3 normal;
    vec3 intensity;
    vec3 lowerCorner;
    vec3 upperCorner;
} light;


layout(location = 0) rayPayloadIn RtParams rtParams;

hitAttribute vec2 attribs;


const float air = 1.0;
const float glass = 1.5;

void main() {
    HitData hit = getHitData(attribs);

    vec3 N = hit.normal;
    vec3 V = -normalize(gl_WorldRayDirection);

    float cos0 = dot(N, V);
    float ni = air;
    float nt = glass;
    bool totalInternalReflection = false;
    if(cos0 < 0){ // incident ray from inside glass
        swap(ni, nt);
        N *= -1;
        float sinSqr0 = 1 - cos0 * cos0;
        float sinC0 = ni/nt;
        totalInternalReflection = sinSqr0 > sinC0 * sinC0;
    }

    rtParams.color = vec3(1, 0, 0);
    rtParams.objectType = OBJECT_TYPE_GLASS;

    if(!totalInternalReflection){
        rtParams.ray.origin = offsetRay(hit.position, -N);
        rtParams.ray.direction = refract(-V, N, ni/nt);
    }else {
        rtParams.ray.origin = offsetRay(hit.position, N);
        rtParams.ray.direction = reflect(-V, N);
    }

}