#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ray_tracing_lang.glsl"
#include "rt_constants.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "util.glsl"

layout(location = 0) rayPayloadIn PhotonGenParams pgParams;

hitAttribute vec2 attribs;

void main(){
    HitData hit = getHitData(attribs);
    pgParams.position = hit.position;

    vec3 I = normalize(gl_WorldRayDirection);
    pgParams.bounceRay.origin = offsetRay(hit.position, hit.normal);
    pgParams.bounceRay.direction = reflect(I, hit.normal);
    pgParams.reflectivity = vec3(1);
    pgParams.diffuse = false;
}