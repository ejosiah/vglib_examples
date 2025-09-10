#version 460

#include "shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 2
#include "atmosphere/atm_uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 1, binding = 11) uniform writeonly image2D global_images[];
layout(set = 1, binding = 11) uniform writeonly image3D global_images_3d[];

#include "atmosphere/common.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} f;

layout(location = 3) flat in int isVisible;
layout(location = 4) noperspective in vec3 distance;

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
    #if 1
    vec4 wireColor = isVisible > 0 ? vec4(0.00,0.20,0.70, 0.5) : vec4(0.40,0.40,0.40,0.5);
    #else
    vec4 wireColor = vec4(0.0, 0.0, 0.0, 0.5);
    #endif

    const float wireScale = 1.1; // scale of the wire in pixel
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);

    worldPos = f.worldPos;
    radiance = vec3(0);

    vec3 L = normalize(globals.lightDirection);
    vec3 normal = -1 + 2 * texture(u_NormalSampler, f.uv).xzy;
    vec3 N = normalize(normal);
    vec3 albedo = f.color;

    AtmosphereParameters Atmosphere = GetAtmosphereParameters();
    vec3 P0 = localUnitsToAtmosphere(worldPos) + vec3(0, Atmosphere.bottom_radius, 0);
    float viewHeight = length(P0);
    const vec3 UpVector = P0 / viewHeight;
    float viewZenithCosAngle = dot(atm.sunDirection, UpVector);
    vec2 uv;
    LutTransmittanceParamsToUv(Atmosphere, viewHeight, viewZenithCosAngle, uv);
    const vec3 trans = texture(transmittanceLUT, uv).rgb;

    float vis = sampleShadowPCF(f.uv);
    float diffuse = max(0, dot(N, L));
    float ambient = 0.1;
    radiance = albedo * (ambient + vis * diffuse * trans);

    radiance = mix(radiance, wireColor.xyz, blendFactor);
}