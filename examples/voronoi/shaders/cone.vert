#version 460

layout(set = 0, binding = 0) buffer Points {
    vec2 points[];
};

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(location = 0) flat out int instanceId;

void main() {
    instanceId = gl_InstanceIndex;
    vec3 offset = vec3(points[instanceId], 0.5);
    vec3 wPos = position + offset;
    gl_Position = vec4(wPos, 1);
}