#version 460 core

layout(set = 0, binding = 0) uniform sampler3D colliderField;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

const int colliderRadius = 2;

void main() {
    ivec2 size = textureSize(colliderField, 0).xy;
    ivec2 base = clamp(ivec2(floor(vUv * vec2(size))), ivec2(0), size - ivec2(1));

    float collider = 1.0;
    for(int y = -colliderRadius; y <= colliderRadius; ++y) {
        for(int x = -colliderRadius; x <= colliderRadius; ++x) {
            ivec2 coord = clamp(base + ivec2(x, y), ivec2(0), size - ivec2(1));
            collider = min(collider, texelFetch(colliderField, ivec3(coord, 0), 0).r);
        }
    }

    if(collider > 0.5) {
        discard;
    }

    fragColor = vec4(1.0, 0.0, 0.0, 0.85);
}
