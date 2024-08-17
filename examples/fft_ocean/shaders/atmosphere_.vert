#version 460

#include "scene.glsl"

layout(location = 0) in vec2 clipPos;
layout(location = 1) in vec2 uv;

layout(location = 0) out struct {
    vec3 position;
    vec3 direction;
} vs_out;

void main() {
    vec4 viewPos = scene.inverse_projection * vec4(clipPos, 1, 1);
    vec4 viewDirection =  scene.inverse_view * vec4(viewPos.xyz, 0);

    vs_out.position = viewPos.xyz;
    vs_out.direction = viewDirection.xyz;

    gl_Position = vec4(clipPos, 0, 1);
}