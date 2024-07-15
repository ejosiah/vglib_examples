#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#include "gltf.glsl"

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
    int framebuffer_texture_id;
    int discard_transmissive;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(location = 0) out struct {
    vec3 localPos;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} vs_out;

layout(location = 8) out flat int drawId;

void main(){
    drawId = gl_DrawID;
    mat4 meshModel = model * meshes[nonuniformEXT(gl_DrawID)].model;
    mat3 nModel = mat3(meshModel);

    vec4 worldPos = meshModel * position;
    vs_out.localPos = position.xyz;
    vs_out.position = worldPos.xyz;
    vs_out.normal = nModel * normal;
    vs_out.tangent = nModel * tanget;
    vs_out.bitangent = nModel * bitangent;
    vs_out.eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.lightPos = vs_out.eyes;
    vs_out.uv = uv;
    gl_PointSize = 5.0;
    gl_Position = projection * view * worldPos;
}