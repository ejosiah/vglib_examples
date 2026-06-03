#ifndef MOON_MATERIAL_COMMON_GLSL
#define MOON_MATERIAL_COMMON_GLSL

#extension GL_EXT_scalar_block_layout : enable

#include "../common.glsl"

#ifndef MOON_MATERIAL_SET
#define MOON_MATERIAL_SET 0
#endif

layout(set = MOON_MATERIAL_SET, binding = 0) uniform texture2D AlbedoTexture;
layout(set = MOON_MATERIAL_SET, binding = 1) uniform texture2D ElevationTexture;
layout(set = MOON_MATERIAL_SET, binding = 2) uniform texture2D DetailTexture;
layout(set = MOON_MATERIAL_SET, binding = 3) uniform texture2D ElevationSGTexture;
layout(set = MOON_MATERIAL_SET, binding = 4) uniform texture2D DetailSGTexture;
layout(set = MOON_MATERIAL_SET, binding = 9) uniform sampler LinearRepeatSampler;
layout(set = MOON_MATERIAL_SET, binding = 10) uniform sampler LinearMirrorVSampler;

#define MOON_REPEAT(imageName) sampler2D(imageName, LinearRepeatSampler)
#define MOON_MIRROR_V(imageName) sampler2D(imageName, LinearMirrorVSampler)

layout(set = MOON_MATERIAL_SET, binding = 5, rgba16f) uniform image2D ElevationSurfaceGradientRW;
layout(set = MOON_MATERIAL_SET, binding = 6, rg16f) uniform image2D DetailSlopeRW;

layout(set = MOON_MATERIAL_SET, binding = 7, scalar) readonly buffer MoonCB {
    uvec2 _ElevationTextureSize;
    uvec2 _DetailTextureSize;
    float _PatchSize;
    float _PatchAmplitude;
    int _NumOctaves;
    uint _Attenuation;
};

#ifndef MOON_MATERIAL_DISABLE_PLANET_CB
layout(set = MOON_MATERIAL_SET, binding = 8, scalar) readonly buffer PlanetCB {
    REAL3_DP _PlanetCenter;
    REAL_DP _PlanetRadius;
};
#endif

vec3 longlat_to_normalized_coordinates(float u, float v) {
    float lon = (u - 0.5) * TWO_PI;
    float lat = (v - 0.5) * PI;
    float cosLat = cos(lat);
    float sinLat = sin(lat);
    float sinLon = sin(lon);
    float cosLon = cos(lon);
    return vec3(cosLon * cosLat, sinLon * cosLat, sinLat);
}

ivec2 wrap_coord(ivec2 coord, uvec2 size) {
    ivec2 limit = ivec2(size);
    return (coord % limit + limit) % limit;
}

#endif // MOON_MATERIAL_COMMON_GLSL
