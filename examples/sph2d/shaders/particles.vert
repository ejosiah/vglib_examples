#version 460

layout(set = 1, binding = 0) buffer ParticlePosition0 {
    vec2 positions[];
};

void main() {
    vec2 position = positions[gl_InstanceIndex];
    gl_Position =  vec4(position, 0, 1);
}