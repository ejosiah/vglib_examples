#version 460 core

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

layout(push_constant) uniform Contants{
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

layout(location = 0) out vec3 viewDirection;

void main(){
    vec4 viewPos = inverse(pc.projection * pc.view) * vec4(pos, 1, 1);
    viewPos /= viewPos.w;
    viewDirection = viewPos.xyz;
    gl_Position = vec4(pos, 1, 1);
}