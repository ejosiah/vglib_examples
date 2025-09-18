#version 460

#include "shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 2
#include "atmosphere/atm_uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 1, binding = 11) uniform writeonly image2D global_images[];
layout(set = 1, binding = 11) uniform writeonly image3D global_images_3d[];

#include "atmosphere/common.glsl"
#include "hash.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec3 color;
    vec2 uv;
} f;

layout(location = 4) flat in int isVisible;

layout(location = 0) out vec3 radiance;
layout(location = 1) out vec4 worldPos;

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

bool debugOn() {
    ivec2 fc = ivec2(gl_FragCoord);
    return globals.mouse.z == 1 && fc.x == globals.mouse.x && fc.y == globals.mouse.y;
}

void main() {
    worldPos = vec4(f.worldPos, gl_FragCoord.z);
    radiance = vec3(0);

    vec3 L = normalize(globals.lightDirection);
    vec3 N = normalize(-1 + 2 * texture(u_NormalSampler, f.uv).xzy);
    vec3 V = normalize(f.viewDirection);

    vec2 gv = (f.uv * 52660)/globals.tileSize;
    vec2 tileUV = fract(gv);
    vec3 albedo = texture(albedoMap, tileUV).rgb;

    if(globals.showTiles == 1) {
        vec2 tileId = floor(gv);
        albedo = globals.colorTiles == 0 ? vec3(tileUV, 0) : hash32(tileId);
    }

    AtmosphereParameters Atmosphere = GetAtmosphereParameters();
    vec3 P0 = localUnitsToAtmosphere(worldPos.xyz) + vec3(0, Atmosphere.bottom_radius, 0);
    float viewHeight = length(P0);
    const vec3 UpVector = P0 / viewHeight;
    float viewZenithCosAngle = dot(atm.sunDirection, UpVector);
    vec2 uv;
    LutTransmittanceParamsToUv(Atmosphere, viewHeight, viewZenithCosAngle, uv);
    const vec3 trans = texture(transmittanceLUT, uv).rgb;

    float vis = sampleShadowPCF(f.uv);
    float ambient = 0.3;

    Material material;
    material.albedo = albedo;
    material.metalness = 0.0;
    material.roughness = 1.0 - texture(roughnessMap, uv).r;
    material.ao = texture(aoMap, uv).r;

    radiance = shadeFragment(material, N, V, L, vis, trans, vec3(ambient));
}