#version 460

#include "shared.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 3) flat in int isVisible;

layout(location = 0) out vec4 color;
layout(location = 1) out vec3 position;
layout(location = 2) out vec3 normal;

float sampleShadowPCF(vec2 uv) {
    ivec2 sz    = textureSize(u_DmapShadowSampler, 0);
    vec2  texel = 1.0 / vec2(sz);

    float sum = 0.0;
    // 3×3 kernel
    for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
        sum += texture(u_DmapShadowSampler, uv + vec2(i, j) * texel).r;
    }
    return sum / 9.0;
}

void main() {
    float visibility = sampleShadowPCF(f.uv);
    color = vec4(f.color, visibility);
    position = f.worldPos;
    normal = texture(u_NormalSampler, f.uv).xyz;
}