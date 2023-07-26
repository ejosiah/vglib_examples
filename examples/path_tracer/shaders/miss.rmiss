#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadIn HitData hitData;

void main(){
    hitData.hit = false;
}