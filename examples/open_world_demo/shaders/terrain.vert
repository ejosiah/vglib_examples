#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec3 vNormal;

void main(){
    vUv = uv;
    vNormal = normal;
    gl_Position = vec4(position, 1);
}