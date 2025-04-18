#version 460

#extension GL_EXT_multiview : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "gltf.glsl"

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color[2];
layout(location = 6) in vec2 uv[2];

layout(set = 0, binding = 0) uniform Light {
    mat4 projection;
    mat4 view[6];
};

layout(set = 1, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(push_constant) uniform Constants {
    mat4 worldTransform;
    vec3 lightPos;
};

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 lightPos;
} vs_out;

void main() {
    uint drawId = gl_DrawID;
    mat4 meshModel = worldTransform * meshes[nonuniformEXT(gl_DrawID)].model;
    mat3 nModel = transpose(inverse(mat3(meshModel)));

    vec4 worldPos = meshModel * position;

    gl_Position = projection * view[gl_ViewIndex] * worldPos;

    vs_out.lightPos = lightPos;
    vs_out.worldPos = worldPos.xyz;
}