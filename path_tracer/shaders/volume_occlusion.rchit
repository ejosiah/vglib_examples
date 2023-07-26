#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_tracing_lang.glsl"
#include "common.glsl"
#include "fresnel.glsl"
#include "scene.glsl"

layout(location = 1) rayPayloadIn OcclusionData ocData;

hitAttribute vec2 attribs;

void main(){

}