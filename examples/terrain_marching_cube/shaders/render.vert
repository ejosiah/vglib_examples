#version 460

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in float ambient_occulsion;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out vec3 color;

void main() {
    color = vec3(abs(normal));
    gl_Position = proj * view * model * vec4(position, 1);
}