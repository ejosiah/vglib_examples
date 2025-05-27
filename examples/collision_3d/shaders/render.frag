#version 460 core

#include "common.glsl"

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 color;
} fs_in;

layout(location = 0) out vec4 fracColor;

void main(){
    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(global.light - fs_in.position);
    vec3 color = fs_in.color * max(0, dot(N, L));
    fracColor.rgb = color;
}