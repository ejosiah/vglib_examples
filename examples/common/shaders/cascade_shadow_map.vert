#version 460

#extension GL_EXT_multiview : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) buffer Cascades {
    mat4 cascadeViewProjMat[];
};

layout(push_constant) uniform Constants {
    mat4 worldTransform;
    int cascadeIndex;
};

void main() {
    gl_Position = cascadeViewProjMat[gl_ViewIndex] * worldTransform * position;
}