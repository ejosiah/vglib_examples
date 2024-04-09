#version 460

layout(set = 1, binding = 0) buffer ParticlePosition0 {
    vec4 positions[];
};

layout(location = 0) out vec4 oColor;

void main() {
    oColor = positions[gl_InstanceIndex].w == 788 ? vec4(1, 0, 0, 1) : vec4(1);
    vec2 position = positions[gl_InstanceIndex].xy;
    gl_Position =  vec4(position, 0, 1);
}