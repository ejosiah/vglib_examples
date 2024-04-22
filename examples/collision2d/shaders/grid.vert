#version 460

layout(location = 0) in vec2 posiiton;
layout(location = 1) in vec2 uv;

layout(location = 0) out struct {
    vec2 uv;
} vs_out;

void main() {
    vs_out.uv = uv;
    gl_Position = gl_Position = vec4(posiiton, 0, 1);
}