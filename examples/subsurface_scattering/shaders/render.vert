#version 460 core

#include "uniforms.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out struct {
    vec4 lightSpacePosition;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec2 uv;
} vs_out;


void main(){
    mat3 nmat = inverse(transpose(mat3(model)));
    vec4 worldPosition = model * position;

    vs_out.position = worldPosition.xyz;
    vs_out.normal = normal;
    vs_out.tangent = tangent;
    vs_out.bitangent = bitangent;
    vs_out.eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.lightSpacePosition = uniforms.lightSpaceMatrix * worldPosition;
    vs_out.uv = uv;

    gl_PointSize = 2.0;
    gl_Position = proj * view * worldPosition;
}