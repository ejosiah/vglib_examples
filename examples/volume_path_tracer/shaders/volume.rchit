#version 460

#include "path_tracing/eval_brdf.glsl"
#include "domain.glsl"

layout(location = 1) rayPayloadIn float hitDistance;

void main() {
    hitDistance = gl_HitT;
}