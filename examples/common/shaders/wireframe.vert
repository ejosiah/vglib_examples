#version 460 core

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) buffer InstanceData {
    mat4 models[];
} instance;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

void main(){
    mat4 worldTransform = instance.models[gl_InstanceIndex];
    gl_PointSize = 2.0;
    gl_Position = proj * view * worldTransform * position;
}