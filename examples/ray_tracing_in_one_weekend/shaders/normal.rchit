#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

void main() {
    vec3 p, n;
    getSurfaceInfo(bc, gl_HitTriangleVertexPositionsEXT, gl_ObjectToWorld, p, n);
    hRec.color = abs(n);
    hRec.wi = vec3(0);
}