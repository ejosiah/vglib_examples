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

void main() {
    HitData hit = getHitData(attribs);
    rtParams.color = vec3(1, 0, 0);
    rtParams.objectType = OBJECT_TYPE_MIRROR;
    rtParams.ray.origin = offsetRay(hit.position, hit.normal);
    rtParams.ray.direction = reflect(normalize(gl_WorldRayDirection), hit.normal);

}