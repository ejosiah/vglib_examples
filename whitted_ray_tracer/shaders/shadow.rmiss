#version 460

#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 1) rayPayloadIn ShadowData sd;

void main(){
    sd.isShadowed = false;
    sd.color = vec3(1, 0, 0);
    sd.visibility = 1.0;
}