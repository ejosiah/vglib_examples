#version 460

layout(set = 1, binding = 0) buffer Positions {
    vec2 positions[];
};

layout(set = 1, binding = 2) buffer Radius {
    float radius[];
};

layout(location = 0) out flat float oRadius;

void main() {
    oRadius = radius[gl_InstanceIndex];
    vec2 position = positions[gl_InstanceIndex].xy;
    gl_Position =  vec4(position, 0, 1);
}