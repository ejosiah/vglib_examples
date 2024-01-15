#version 460 core

#extension GL_EXT_nonuniform_qualifier : require

layout(push_constant) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(set = 0, binding = 0) buffer MeshData {
    mat4 model;
    mat4 model_inverse;
    int materialId;
} meshes[];

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;


layout(location = 0) out struct {
    vec3 tanget;
    vec3 bitangent;
    vec3 color;
    vec3 pos;
    vec3 localPos;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} vs_out;

layout(location = 10)  out flat uint drawId;

void main(){
    vs_out.tanget = tanget;
    vs_out.bitangent = bitangent;
    vs_out.uv = uv;
    drawId = gl_DrawID;
    mat4 mModel = meshes[nonuniformEXT(gl_DrawID)].model;
    vec4 worldPos = mModel * position;
    vs_out.pos = worldPos.xyz;
    vs_out.localPos = position.xyz;
    vs_out.normal = mat3(mModel) * normal;
    vs_out.eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.lightPos = vs_out.eyes;

    gl_Position = projection * view * worldPos;
}