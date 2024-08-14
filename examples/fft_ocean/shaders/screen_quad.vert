#version 460

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 vs_uv;

void main() {
    vs_uv = uv;
    gl_Position = vec4(position, 0, 1);
}