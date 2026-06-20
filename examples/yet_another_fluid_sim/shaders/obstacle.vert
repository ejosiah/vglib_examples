#version 460

layout(push_constant) uniform Constants {
    mat4 transform;
    vec4 color;
    vec2 position;
    vec2 velocity;
    vec2 domainMin;
    vec2 domainMax;
    float radius;
    float size;
    uint scene;
};

layout(location = 0) out vec4 oColor;
layout(location = 1) flat out float oRadius;

void main() {
    oColor = color;
    oRadius = radius;
    gl_PointSize = size * 1.01;
    gl_Position = transform * vec4(position, 0.5f, 1.0f);
}
