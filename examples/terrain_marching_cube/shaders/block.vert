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

vec4 colors[3] = vec4[3](vec4(1, 0, 0, 1), vec4(0, 1, 0, 1), vec4(1, 0, 0, 1));

void main(){

//    vColor = colors[block[bid].state];
    vColor = vec4(1);
    vUv = uv;
    vec3 center  = block[gl_InstanceIndex].aabb;
//    vec3 center  = draw[gl_InstanceIndex].aabb.xyz;
    vec4 sPosition = model * position;
    vec4 local_position =  vec4(center + sPosition.xyz, 1);
//    const mat4 grid_to_world = camera_info.grid_to_world;
    const mat4 grid_to_world = mat4(1);

    gl_Position = proj * view * grid_to_world * local_position;
}