#version 460

#extension GL_ARB_shader_viewport_layer_array : enable
#extension GL_EXT_multiview : enable

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) uniform Light {
    mat4 projection;
    mat4 view[6];
};

layout(push_constant) uniform CONSTANTS {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 position;
    float farPlane;
} globals;

layout(location = 0) out struct {
    vec3 viewSpacePos;
} vs_out;

layout(location = 2) out flat float farPlane;

void main() {
    gl_Layer = gl_ViewIndex;
    vec4 vsPosition = view[gl_ViewIndex] * globals.model * position;
    vs_out.viewSpacePos = vsPosition.xyz;

    farPlane = globals.farPlane;

    gl_Position = projection * vsPosition;
}