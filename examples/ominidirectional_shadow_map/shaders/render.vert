#version 460

layout(push_constant) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(location = 0) out struct {
    vec3 position;
    vec3 normal;
    vec2 uv;
} vs_out;

layout(location = 3) out flat int drawId;

void main() {
    mat3 nModel = transpose(inverse(mat3(model)));
    drawId = gl_DrawID;

    vec4 worldPos = position * model;
    vs_out.position = worldPos.xyz;
    vs_out.normal = normal * nModel;
    vs_out.uv = uv;

    gl_Position = projection * view * worldPos;
}