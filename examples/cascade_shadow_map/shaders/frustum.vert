#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) buffer Cascades {
    mat4 cascadeViewProjMat[];
};

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
    int cascadeIndex;
};

vec3 cascadeColors[6] = vec3[6](
    vec3(1.0f, 0.25f, 0.25f),
    vec3(0.25f, 1.0f, 0.25f),
    vec3(0.25f, 0.25f, 1.0f),
    vec3(1.0f, 1.0f, 0.25f),
    vec3(0.25f, 1.0f, 1.0f),
    vec3(1.0f, 0.25f, 1.0f)
);

layout(location = 0) out vec3 oColor;

void main() {
    vec4 worldPos = inverse(cascadeViewProjMat[cascadeIndex]) * position;
    worldPos /= worldPos.w;

    oColor = cascadeColors[cascadeIndex];
    gl_Position = proj * view * model * worldPos;
}