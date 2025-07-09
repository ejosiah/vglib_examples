#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

void main() {
    hRec.hit = false;
    float t = 0.5 * (normalize(gl_WorldRayDirection).y + 1);
    hRec.color = mix(vec3(1.0), vec3(0.5, 0.7, 1.0), t);
}