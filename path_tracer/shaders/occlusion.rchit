#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 1) rayPayloadIn OcclusionData occData;

void main(){
    occData.transmission = vec3(0);
    occData.isShadowed = true;
    occData.Continue = false;
}