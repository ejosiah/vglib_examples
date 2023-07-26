#version 460 core

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out struct {
    vec4 color;
    vec3 position;
    vec3 normal;
    vec2 uv;
} vs_out;


void main(){
    vs_out.position = (position * model).xyz;
    vs_out.color = color;
    vs_out.normal = inverse(transpose(mat3(model))) * normal;
    vs_out.uv = uv;
    gl_PointSize = 2.0;
    gl_Position = proj * view * model * position;
}