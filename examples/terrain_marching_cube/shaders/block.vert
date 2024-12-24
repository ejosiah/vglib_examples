#version 460 core

#include "models.glsl"

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

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;


void main(){
    const uint bid = gl_InstanceIndex;
    if(bid >= block_id) return;
    vColor = color;
    vUv = uv;
    vec3 center  = block[bid].aabb;
    vec4 worldPos =  vec4(center + position.xyz, 1);
    const mat4 grid_to_world = camera_info.grid_to_world;

    gl_Position = proj * view * grid_to_world  * worldPos;
}