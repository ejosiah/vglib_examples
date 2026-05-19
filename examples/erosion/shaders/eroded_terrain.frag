#version 460

#include "vista/shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 3
#include "vista/atmosphere/atm_uniforms.glsl"
#include "vista/atmosphere/common.glsl"
#include "vista/tiling_support.glsl"

const float materialNearDistance = 20.0;
const float materialMidDistance = 500.0;
const float materialFarDistance = 1000.0;
const float materialNearScale = 1.0;
const float materialMidScale = 0.25;
const float materialFarScale = 0.125;

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

float sampleShadowPCF(vec2 uv) {
    ivec2 size = textureSize(u_DmapShadowSampler, 0);
    vec2 texel = 1.0 / vec2(size);

    float sum = 0.0;
    for(int y = -1; y <= 1; ++y) {
        for(int x = -1; x <= 1; ++x) {
            sum += texture(u_DmapShadowSampler, uv + vec2(x, y) * texel).r;
        }
    }
    return sum / 9.0;
}

float softenShadowVisibility(float visibility) {
    float shadowMinVisibility = 1.0 - clamp(globals.shadowDarkness, 0.0, 1.0);
    return mix(shadowMinVisibility, 1.0, clamp(visibility, 0.0, 1.0));
}

vec3 mixWireFrame(vec3 srcColor) {
    vec3 wireColor = isVisible > 0 ? vec3(0.00, 0.20, 0.70) : vec3(0.40);
    const float wireScale = 1.1;
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float t = exp2(-nearestDistance / wireScale);

    return mix(srcColor, wireColor, t);
}

float terrainTileScale() {
    float x = length(globals.modelMatrix[0].xyz);
    float y = length(globals.modelMatrix[1].xyz);
    float z = length(globals.modelMatrix[2].xyz);
    float terrainSpan = max(max(x, y), z);
    return max(terrainSpan / max(globals.tileSize.x, 0.001), 1.0);
}

vec3 checkerboardPattern(vec2 tileCoord) {
    vec2 tileId = floor(tileCoord);
    float t = step(1.0, mod(tileId.x + tileId.y, 2.0));
    return vec3(0.2 + 0.8 * t);
}

vec2 mappedMaterialUV(float scale, float mosaicRotation) {
    float gridView;
    return uberMapping(f.uv, scale, 1.0, 0.0, 0.0, 0.0, mosaicRotation, 1.5, gridView);
}

float materialDistanceFade(float cameraDepth, float nearDistance, float farDistance) {
    return clamp((cameraDepth - nearDistance) / max(farDistance - nearDistance, 1e-6), 0.0, 1.0);
}

vec2 materialUV(float scaleFactor, float mosaicRotation) {
    return mappedMaterialUV(terrainTileScale() * scaleFactor, mosaicRotation);
}

vec2 fadedMaterialUV(float cameraDepth) {
    float nearToMid = materialDistanceFade(cameraDepth, materialNearDistance, materialMidDistance);
    float midToFar = materialDistanceFade(cameraDepth, materialMidDistance, materialFarDistance);
    vec2 mappedNear = materialUV(materialNearScale, 120.0);
    vec2 mappedMid = materialUV(materialMidScale, 105.0);
    vec2 mappedFar = materialUV(materialFarScale, 90.0);
    return mix(mix(mappedNear, mappedMid, nearToMid), mappedFar, midToFar);
}

vec3 getTileColor(float cameraDepth) {
    vec2 tileCoord = fadedMaterialUV(cameraDepth);
    if(globals.tileColor == 0) {
        return vec3(fract(tileCoord), 0.0);
    }
    if(globals.tileColor == 1) {
        return checkerboardPattern(tileCoord);
    }
    return hash32(floor(tileCoord));
}

vec3 applyMaterialNormal(vec3 baseNormal, vec3 mapNormal) {
    vec3 tangent = vec3(1.0, 0.0, 0.0);
    tangent = tangent - baseNormal * dot(baseNormal, tangent);
    if(dot(tangent, tangent) < 1e-4) {
        tangent = vec3(0.0, 0.0, 1.0) - baseNormal * dot(baseNormal, vec3(0.0, 0.0, 1.0));
    }
    tangent = normalize(tangent);
    vec3 bitangent = normalize(cross(baseNormal, tangent));

    return normalize(tangent * mapNormal.x + bitangent * mapNormal.y + baseNormal * mapNormal.z);
}

vec3 hsvToRgb(vec3 hsv) {
    vec3 p = abs(fract(hsv.xxx + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return hsv.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), hsv.y);
}

vec3 waterFlowColor(vec2 uv) {
    vec2 velocity = texture(u_WaterFlowSampler, uv).rg;
    float speed = length(velocity);
    if(speed <= 1e-8) {
        return vec3(0.01, 0.03, 0.06);
    }

    float angle = atan(velocity.y, velocity.x);
    float hue = fract(angle / (2.0 * PI) + 1.0);
    float strength = clamp(log2(1.0 + speed * max(globals.waterFlowScale, 0.0)), 0.0, 1.0);
    vec3 directionColor = hsvToRgb(vec3(hue, 0.85, 1.0));

    return mix(vec3(0.01, 0.03, 0.06), directionColor, strength);
}

Material samplePatchyDirtMaterial(vec2 uv) {
    Material material;
    material.metalness = 0.0;
    material.albedo = texture(dirtAlbedoMap, uv).rgb;
    material.normal = texture(dirtNormalMap, uv).xyz * 2.0 - 1.0;
    material.roughness = clamp(texture(dirtRoughnessMap, uv).r, 0.04, 1.0);
    material.ao = texture(dirtAoMap, uv).r;
    material.blend = 0.0;

    return material;
}

Material sampleFadedPatchyDirtMaterial(float cameraDepth) {
    float nearToMid = materialDistanceFade(cameraDepth, materialNearDistance, materialMidDistance);
    float midToFar = materialDistanceFade(cameraDepth, materialMidDistance, materialFarDistance);
    vec2 mappedNear = materialUV(materialNearScale, 120.0);
    vec2 mappedMid = materialUV(materialMidScale, 105.0);
    vec2 mappedFar = materialUV(materialFarScale, 90.0);

    Material nearMaterial = samplePatchyDirtMaterial(fract(mappedNear));
    Material midMaterial = samplePatchyDirtMaterial(fract(mappedMid));
    Material farMaterial = samplePatchyDirtMaterial(fract(mappedFar));
    Material material = mix(
        mix(nearMaterial, midMaterial, nearToMid),
        farMaterial,
        midToFar
    );

    material.normal = normalize(material.normal);

    if(globals.showTiles == 1) {
        material.albedo = getTileColor(cameraDepth);
    }

    return material;
}

float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

float disneyDiffuseFactor(float NdotV, float NdotL, float LdotH, float roughness) {
    float fd90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = 1.0 + (fd90 - 1.0) * pow5(1.0 - NdotL);
    float viewScatter = 1.0 + (fd90 - 1.0) * pow5(1.0 - NdotV);
    return lightScatter * viewScatter;
}

vec3 shadeDisneyDiffuse(Material material, vec3 N, vec3 V, vec3 L, float visibility, vec3 sunTransmittance, vec3 ambientIrradiance) {
    float roughness = clamp(material.roughness, 0.04, 1.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    vec3 H = normalize(L + V);
    float LdotH = max(dot(L, H), 0.0);

    float diffuseFactor = disneyDiffuseFactor(NdotV, NdotL, LdotH, roughness);
    vec3 diffuse = material.albedo * diffuseFactor / PI;

    vec3 radiance = vec3(4.0) * sunTransmittance * visibility;
    vec3 direct = diffuse * radiance * NdotL;
    vec3 ambient = material.albedo * ambientIrradiance * material.ao;
    return direct + ambient;
}

vec3 shadeLambertian(Material material, vec3 N, vec3 V, vec3 L, float visibility, vec3 sunTransmittance, vec3 ambientIrradiance) {
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = material.albedo / PI;

    vec3 radiance = vec3(4.0) * sunTransmittance * visibility;
    vec3 direct = diffuse * radiance * NdotL;
    vec3 ambient = material.albedo * ambientIrradiance * material.ao;
    return direct + ambient;
}

vec3 sunTransmittanceAtSurface() {
    AtmosphereParameters atmosphere = GetAtmosphereParameters();
    vec3 position = localUnitsToAtmosphere(f.worldPos) + vec3(0.0, atmosphere.bottom_radius, 0.0);
    float viewHeight = length(position);
    vec3 up = position / viewHeight;
    float viewZenithCosAngle = dot(atm.sunDirection, up);

    vec2 uv;
    LutTransmittanceParamsToUv(atmosphere, viewHeight, viewZenithCosAngle, uv);
    return texture(transmittanceLUT, uv).rgb;
}

void main() {
    worldPos = vec4(f.worldPos, gl_FragCoord.z);

    float cameraDepth = max(-f.viewSpacePos.z, 0.0);
    if(visualizeDepthFade()) {
        radiance = vec3(clamp(cameraDepth / materialFarDistance, 0.0, 1.0));
        return;
    }

    if(visualizeWaterFlow()) {
        radiance = mixWireFrame(waterFlowColor(f.uv));
        return;
    }

    Material material = sampleFadedPatchyDirtMaterial(cameraDepth);
    vec3 terrainNormal = normalize(-1.0 + 2.0 * texture(u_NormalSampler, f.uv).xzy);
    vec3 N = applyMaterialNormal(terrainNormal, material.normal);
    vec3 V = normalize(atm.cameraPosition - f.worldPos);
    vec3 L = normalize(globals.lightDirection);

    float visibility = softenShadowVisibility(sampleShadowPCF(f.uv));
    vec3 sunTransmittance = sunTransmittanceAtSurface();

    vec3 lit = shadeLambertian(material, N, V, L, visibility, sunTransmittance, vec3(0.22));

    radiance = mixWireFrame(lit);
}
