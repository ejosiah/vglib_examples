#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "rt_constants.glsl"

layout(location = 1) rayPayloadIn bool isVisible;

void main() {
    isVisible = true;
}