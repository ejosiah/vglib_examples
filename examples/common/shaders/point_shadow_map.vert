#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform Camera {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 position;
    float farPlane;
} light;

layout(location = 0) out struct {
    vec3 worldPos;
} vs_out;

layout(location = 1) out flat int drawId;

void main() {
    vs_out.worldPos = (light.model * position).xyz;
    drawId = gl_DrawID;
    mat4 lightViewProjection = light.projection * light.view;
    gl_Position = lightViewProjection * vec4(vs_out.worldPos, 1);
}