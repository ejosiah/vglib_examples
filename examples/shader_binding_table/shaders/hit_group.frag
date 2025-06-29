#version 460

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) buffer COLORS {
    vec4 hit_groups[];
};

layout(push_constant) uniform Constants {
    int num_hit_groups;
};

void main() {
    vec2 uv = vUv;
    int id = int(floor(uv.x * 32.));

    if(uv.y < 0.95 || id >= num_hit_groups) discard;


    fragColor = hit_groups[id];
}