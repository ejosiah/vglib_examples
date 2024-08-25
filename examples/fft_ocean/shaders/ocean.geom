#version 460

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

#include "scene.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} gs_in[];

layout(location = 3) flat in int patchId_in[];

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} gs_out;

layout(location = 3) flat out int patchId;

void main() {

    vec3 e0 = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
    vec3 e1 = gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz;

    vec3 normal = normalize(cross(e0, e1));

    for(int i = 0; i < 3; ++i) {
        gl_Position = scene.mvp * gl_in[i].gl_Position;
        gs_out.worldPos = gs_in[i].worldPos;
        gs_out.normal = gs_in[i].normal;
//        gs_out.normal = normal;
        gs_out.uv = gs_in[i].uv;
        patchId = patchId_in[i];
        EmitVertex();
    }
}