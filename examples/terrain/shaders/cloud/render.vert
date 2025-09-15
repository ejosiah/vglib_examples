#version 460 core

#define ATMOSPHERE_UNIFORM_SET 2
#include "../atmosphere/atm_uniforms.glsl"

layout(location = 0) in vec2 clipPos;
layout(location = 1) in vec2 uv;

layout(location = 0) out struct {
    vec3 viewDirection;
    vec2 uv;
} vs_out;

void main(){
    vec4 viewPos = atm.inverseProjection * vec4(clipPos, 1, 1);
    vec4 viewDirection =  atm.inverseView * vec4(viewPos.xyz, 0);

    vs_out.viewDirection = viewDirection.xyz;
    vs_out.uv = uv;

    gl_Position = vec4(clipPos, 1, 1);
}