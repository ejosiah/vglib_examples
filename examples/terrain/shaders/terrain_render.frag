#version 460

#include "shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 2
#include "atmosphere/atm_uniforms.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 3) flat in int isVisible;

layout(location = 0) out vec4 fragColor;

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
    vec3 L = normalize(globals.lightDirection);
    vec3 normal = -1 + 2 * texture(u_NormalSampler, f.uv).xzy;
    vec3 N = normalize(normal);
    vec3 albedo = f.color;

    float vis = sampleShadowPCF(f.uv);
    float diffuse = max(0, dot(N, L));
    float ambient = 0.2;
    vec3 radiance = albedo * (ambient + vis * diffuse);
    radiance = pow(radiance, vec3(0.454));
    fragColor = vec4(radiance, 1);
}