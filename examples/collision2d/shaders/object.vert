#version 460

layout(set = 1, binding = 0) buffer Positions {
    vec2 positions[];
};

layout(set = 1, binding = 2) buffer Radius {
    float radius[];
};

layout(set = 1, binding = 3) buffer Colors {
    vec4 colors[];
};

layout(location = 0) out flat float oRadius;
layout(location = 1) out vec4 iColor;

void main() {
    oRadius = radius[gl_InstanceIndex];
    iColor = colors[gl_InstanceIndex];
    vec2 position = positions[gl_InstanceIndex].xy;
    gl_Position =  vec4(position, 0, 1);
}