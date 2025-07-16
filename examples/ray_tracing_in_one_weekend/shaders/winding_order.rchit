#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

void main() {
    if(gl_HitKind == gl_HitKindFrontFacingTriangle) {
        hRec.color = vec3(0, 0, 1);
    }else {
        hRec.color = vec3(1, 0, 0);
    }
    hRec.wi = vec3(0);
}