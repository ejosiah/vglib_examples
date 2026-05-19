#version 460

#include "../shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 3
#include "../atmosphere/atm_uniforms.glsl"
#include "triplaner_mapping.glsl"
#include "../tiling_support.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 1, binding = 11) uniform writeonly image2D global_images[];
layout(set = 1, binding = 11) uniform writeonly image3D global_images_3d[];

#include "../atmosphere/common.glsl"

float heightScale = 1601;
float terrainSize = 52660;
const float fadeOffset = 600;
const float fadeLength = 2000;
float uvScale = 52660/(globals.tileSize.x);
float gridView;
float farScale = 64;

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewSpacePos;
    vec3 viewDirection;
    vec3 normal;
    vec3 color;
    vec2 uv;
} f;

layout(location = 6) flat in int isVisible;
layout(location = 7) noperspective in vec3 distance;

layout(location = 0) out vec3 radiance;
layout(location = 1) out vec4 worldPos;

float fade = cameraDepthFade(f.viewSpacePos, fadeOffset, fadeLength);

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

vec3 mixWireFrame(vec3 srcColor) {
    #if 1
    vec3 wireColor = isVisible > 0 ? vec3(0.00,0.20,0.70) : vec3(0.40,0.40,0.40);
    #else
    vec3 wireColor = vec3(0.0, 0.0, 0.0);
    #endif

    const float wireScale = 1.1; // scale of the wire in pixel
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);

    float t =  exp2(-nearestDistance / wireScale);

    return mix(srcColor, wireColor, t);
}

float remap(float x, float a, float b, float c, float d) {
    return (((x - a) / (b - a)) * (d - c)) + c;
}

float tileScale = 1/globals.tileSize.x;

vec4 sampleTexture(sampler2D aSampler, vec3 position, vec3 normal, vec2 tileUV) {
    if(useTriplanerMapping()) {
        return triplanerSample(aSampler, position, normal, tileScale);
    }
    return texture(aSampler, tileUV);
}



vec3 checkerboardPattern(vec2 uv) {
    if(useTriplanerMapping()) {
        TriplanarCoords coords = getTriplanarCoords(f.worldPos, f.normal, tileScale);
        vec2 tileId = floor(coords.uvX);
        float tx = step(1, mod(tileId.x + tileId.y, 2.0));
        vec3 cx = vec3(0.2 + 0.8 * tx);

        tileId = floor(coords.uvY);
        float ty = step(1, mod(tileId.x + tileId.y, 2.0));
        vec3 cy = vec3(0.2 + 0.8 * ty);

        tileId = floor(coords.uvZ);
        float tz = step(1, mod(tileId.x + tileId.y, 2.0));
        vec3 cz = vec3(0.2 + 0.8 * tz);

        return cx * coords.blendWeights.x + cy * coords.blendWeights.y + cz * coords.blendWeights.z;
    }
    vec2 gv = (uv * 52660)/globals.tileSize;
    vec2 tileUV = fract(gv);
    vec2 tileId = floor(gv);
    float t = step(1, mod(tileId.x + tileId.y, 2.0));
    return vec3(0.2 + 0.8 * t);
}

vec3 getTileUVColor(vec2 uv) {
    if(useTriplanerMapping()) {
        TriplanarCoords coords = getTriplanarCoords(f.worldPos, f.normal, tileScale);

        return vec3(mod(coords.uvX, 1), 0) * coords.blendWeights.x + vec3(mod(coords.uvY, 1), 0) * coords.blendWeights.y + vec3(mod(coords.uvZ, 1), 0) * coords.blendWeights.z;
    }
    vec2 mapped_near = uberMapping(f.uv, uvScale, 1, 0, 0, 0, 120, 1.5, gridView);
    vec2 mapped_far = uberMapping(f.uv, uvScale / farScale, 1, 0, 0, 0, 90, 1.5, gridView);
    float fd = cameraDepthFade(f.viewSpacePos, fadeOffset, fadeLength);
    vec2 mapped = mix(mapped_near, mapped_far, fd);

    vec2 tileUV = fract(mapped);
    return vec3(tileUV, 0);
}

vec3 getTileColor(vec2 uv) {
    if(globals.tileColor == 0) {
        return getTileUVColor(uv);
    }else if(globals.tileColor == 1) {
        return checkerboardPattern(uv);
    }else {
        vec2 mapped_near = uberMapping(f.uv, uvScale, 1, 0, 0, 0, 120, 1.5, gridView);
        vec2 mapped_far = uberMapping(f.uv, uvScale / farScale, 1, 0, 0, 0, 90, 1.5, gridView);
        float fd = cameraDepthFade(f.viewSpacePos, fadeOffset, fadeLength);
        vec2 mapped = mix(mapped_near, mapped_far, fd);

        vec2 tileUV = fract(mapped);
        vec2 tileId = floor(mapped);
        return hash32(tileId);
    }
}


Material getMaterial(vec2 uv, vec2 tileId, vec2 tileUV) {
    Material dirt, grass;

    dirt.metalness = 0.0;
    dirt.albedo = sampleTexture(dirtAlbedoMap, f.worldPos, f.normal, tileUV).rgb;
    dirt.normal = -1 + 2 * sampleTexture(dirtNormalMap, f.worldPos, f.normal, tileUV).xzy;
    dirt.roughness = 1.0 - sampleTexture(dirtRoughnessMap, f.worldPos, f.normal, tileUV).r;
    dirt.ao = sampleTexture(dirtAoMap, f.worldPos, f.normal, tileUV).r;

    grass.metalness = 0.0;
    grass.albedo = sampleTexture(grassAlbedoMap, f.worldPos, f.normal, tileUV).rgb;
    grass.normal = -1 + 2 * sampleTexture(grassNormalMap, f.worldPos, f.normal, tileUV).xzy;
    grass.roughness = 1.0 - sampleTexture(grassRoughnessMap, f.worldPos, f.normal, tileUV).r;
    grass.ao = sampleTexture(grassAoMap, f.worldPos, f.normal, tileUV).r;

    float t = texture(u_DmapSampler, uv).r;

    t = remap(t, globals.blendMin, globals.blendMax, 0, 1);

    Material material;
    material.metalness = 0.0;
    material.albedo = mix(dirt.albedo, grass.albedo, t);
    material.normal = mix(dirt.normal, grass.normal, t);
    material.roughness = mix(dirt.roughness, grass.roughness, t);
    material.ao = mix(dirt.ao, grass.ao, t);
    material.blend = t;

    return material;
}

void main() {
    worldPos = vec4(f.worldPos, gl_FragCoord.z);
    radiance = vec3(0);

    if(visualizeDepthFade()) {
        radiance = vec3(fade);
        return;
    }

    vec3 L = normalize(globals.lightDirection);

    vec2 terrainScale = heightScale / globals.tileSize;
    vec3 normal = -1 + 2 * texture(u_NormalSampler, f.uv).xzy;
    vec3 N = normalize(normal);
    vec3 V = normalize(atm.cameraPosition - f.worldPos);

    vec2 gv = (f.uv * 52660)/(globals.tileSize * 64);
    vec2 tileId = floor(gv);
    vec2 tileUV = fract(gv);

    vec2 mapped_near = uberMapping(f.uv, uvScale, 1, 0, 0, 0, 120, 1.5, gridView);
    vec2 mapped_far = uberMapping(f.uv, uvScale / farScale, 1, 0, 0, 0, 90, 1.5, gridView);

    vec2 tile_near = fract(mapped_near);
    vec2 tile_far = fract(mapped_far);

    Material mClose = getMaterial(f.uv, tileId, tile_near);
    Material mFar =  getMaterial(f.uv, tileId, tile_far);

    Material material = mix(mClose, mFar, fade);
    material.albedo = globals.showTiles != 1 ? material.albedo : getTileColor(f.uv);
    vec3 dN = material.normal;
    N = normalize(vec3(N.xy + dN.xy, N.z*dN.z));

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
    vec3 lit = vec3(0.0);
    if(globals.useLeadrLighting == 1) {
        float momentsLod = max(textureQueryLod(u_SlopeMoments0Sampler, f.uv).x, 0.0);
        lit = shadeFragment_LEADR(material, N, V, L, f.uv, momentsLod, vis, trans, vec3(ambient));
    } else {
        lit = shadeFragment(material, N, V, L, vis, trans, vec3(ambient));
    }
    radiance = mixWireFrame(lit);
}
