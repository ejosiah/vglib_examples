#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out struct {
    vec4 color;
    vec3 worldPos;
    vec3 viewPos;
    vec3 normal;
    vec3 tanget;
    vec3 bitangent;
    vec2 uv;
} vs_out;

void main() {
    vec4 worldPos = model * position;
    mat3 nomMat = transpose(inverse(mat3(model)));
    vec3 N = nomMat * normal;
    vec3 T = nomMat * tanget;
    vec3 B = nomMat * bitangent;

    vs_out.worldPos = worldPos.xyz;
    vs_out.normal = N;
    vs_out.tanget = T;
    vs_out.bitangent = B;
    vs_out.viewPos = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.color = color;
    vs_out.uv = uv * 2;

    gl_Position = proj * view * worldPos;
}