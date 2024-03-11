#version 460

layout(set = 0, binding = 0) buffer Globals {
    mat4 projection;
    int maxPoints;
    int numPoints;
    float convergenceRate;
    int width;
    int height;
};

layout(set = 0, binding = 1) buffer Points {
    vec2 points[];
};

layout(location = 0) in vec3 position;

layout(location = 0) flat out int instanceId;
layout(location = 1) out vec2 generator;

void main() {
    instanceId = gl_InstanceIndex;
    generator = points[instanceId];
    vec3 offset = vec3(points[instanceId], 0.5);
    offset.xy = 2 * (offset.xy/vec2(width, height)) - 1;
    vec3 wPos = position + offset;
    gl_Position =  vec4(wPos, 1);
}