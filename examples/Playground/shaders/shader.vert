#version 460

layout(set = 0, binding = 0, std140) uniform CameraUniform {
    mat4 view;
    mat4 projection;
};

layout(push_constant) uniform Constants {
    mat4 model;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vg_Pervertex {
    vec3 normal;
    vec3 uv;
} vs_out;

void main() {
    gl_Position = position;
}