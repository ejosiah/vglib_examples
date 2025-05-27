#version 460 core

#include "common.glsl"

layout(set = 1, binding = 0, scalar) buffer Positions {
    vec3 data[];
} particles[2];

layout(set = 1, binding = 2, scalar) buffer Colors {
    vec3 colors[];
};

layout(set = 1, binding = 3, scalar) buffer Radius {
    float radius[];
};

layout(push_constant) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(location = 0) out struct {
    vec3 position;
    vec3 normal;
    vec3 color;
} vs_out;

void main() {
    uint index = gl_InstanceIndex;
    vec3 p = particles[CURRENT].data[index] + position.xyz * global.radius;

    vs_out.color = colors[index];
    vs_out.normal = normal;
    gl_Position = proj * view * model * vec4(p, 1);
}