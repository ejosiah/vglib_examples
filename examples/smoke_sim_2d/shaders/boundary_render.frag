#version 460 core

layout(set = 0, binding = 0) uniform sampler2D boundaryField;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

const int boundaryRadius = 2;

void main() {
    ivec2 size = textureSize(boundaryField, 0);
    ivec2 base = clamp(ivec2(floor(vUv * vec2(size))), ivec2(0), size - ivec2(1));

    float boundary = 0.0;
    for(int y = -boundaryRadius; y <= boundaryRadius; ++y) {
        for(int x = -boundaryRadius; x <= boundaryRadius; ++x) {
            ivec2 coord = clamp(base + ivec2(x, y), ivec2(0), size - ivec2(1));
            boundary = max(boundary, texelFetch(boundaryField, coord, 0).r);
        }
    }

    if(boundary <= 0.5) {
        discard;
    }

    fragColor = vec4(1.0, 0.0, 0.0, 0.85);
}
