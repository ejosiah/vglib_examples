#version 460

#include "shared.glsl"

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 2
#define ATMOSPHERE_LUT_SET 3
#include "atmosphere/bruneton_api.glsl"
#include "tone_mapping.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 0) out vec3 radiance;
layout(location = 1) out vec3 worldPos;

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
    worldPos = f.worldPos;
    radiance = vec3(0);

    vec3 L = normalize(globals.lightDirection);
    vec3 normal = -1 + 2 * texture(u_NormalSampler, f.uv).xzy;
    vec3 N = normalize(normal);
    vec3 albedo = f.color;

    float visiblity = sampleShadowPCF(f.uv);

    vec3 earthCenter = atmosphereToLocalUnits(vec3(0, -ATMOSPHERE.bottom_radius, 0));
    vec3 point = f.worldPos - earthCenter;
    vec3 skyIrradiance;
    vec3 sunIrradiance = GetSunAndSkyIrradiance(point, N, L, skyIrradiance);

    radiance = (albedo / PI) * (skyIrradiance + sunIrradiance * visiblity) ;

}
