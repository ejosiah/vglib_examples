#version 460 core

layout(push_constant) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(set = 4, binding = 0) uniform LightSpaceMatrix {
    mat4 lightViewProjection;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(location = 0) out struct {
    vec4 lightSpacePos;
    vec3 position;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} vs_out;

layout(location = 6) out flat int drawId;

void main(){
    drawId = gl_DrawID;
    vec4 worldPos = model * position;
    vs_out.position = worldPos.xyz;
    vs_out.normal = mat3(model) * normal;
    vs_out.eyes = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.lightPos = vs_out.eyes;
    vs_out.uv = uv;
    vs_out.lightSpacePos = lightViewProjection * worldPos;

    gl_Position = projection * view * worldPos;
//    gl_Position = lightViewProjection * worldPos;
}