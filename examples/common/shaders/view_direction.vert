#version 460 core

layout(location = 0) in vec2 clipPos;
layout(location = 1) in vec2 uv;

layout(push_constant) uniform Contants{
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

layout(location = 0) out struct {
    vec3 position;
    vec3 direction;
} vs_out;

void main(){
    vec4 viewPos = inverse(pc.projection) * vec4(clipPos, 1, 1);
    vec4 viewDirection =  inverse(pc.view) * vec4(viewPos.xyz, 0);
    viewPos = inverse(pc.view) * viewPos;

    vs_out.position = viewPos.xyz/viewPos.w;
    vs_out.direction = viewDirection.xyz;

    gl_Position = vec4(clipPos, 1, 1);
}