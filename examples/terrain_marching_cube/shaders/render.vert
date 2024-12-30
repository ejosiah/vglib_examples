#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 normal;
layout(location = 2) in float ambient_occulsion;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};


void main() {
    gl_Position = proj * view * model * vec4(position, 1);
}