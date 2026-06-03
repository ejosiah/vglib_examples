#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 3
#define ATMOSPHERE_LUT_SET 4
#include "atmosphere/bruneton_api.glsl"

#define WATER_DATA_SET 5
#define NUM_SG_BANDS 4u
#define SG_BAND_ATTENUATION_START 10.0
#define SG_BAND_ATTENUATION_END 20.0
#define BAND_ROUGHNESS_START 15.0
#define BAND_ROUGHNESS_END 30.0

layout(set = 1, binding = 0, scalar) readonly buffer UpdateCB {
    REAL4X4_DP _UpdateViewProjectionMatrix;
    REAL4X4_DP _UpdateInvViewProjectionMatrix;
    REAL4_DP _FrustumPlanes[6];
    REAL3_DP _UpdateCameraPosition;
    REAL3_DP _UpdateCameraForward;
    REAL_DP _TriangleSize;
    REAL_DP _UpdateFOV;
    REAL_DP _UpdateFarPlaneDistance;
    uint _MaxSubdivisionDepth;
};

layout(set = 1, binding = 1, scalar) readonly buffer PlanetCB {
    REAL3_DP _PlanetCenter;
    REAL_DP _PlanetRadius;
};

layout(set = 2, binding = 0) uniform AtmosphereInfo {
    mat4 inverse_model;
    mat4 inverse_view;
    mat4 inverse_projection;

    vec4 camera;
    vec4 earthCenter;
    vec4 sunDirection;
    vec4 whitePoint;
    vec2 sunSize;
    float exposure;
} atmosphereInfo;

layout(set = WATER_DATA_SET, binding = 0, scalar) readonly buffer DeformationData {
    vec4 _DeformationPatchSize;
    vec4 _DeformationPatchRoughness;
    float _DeformationChoppiness;
    int _DeformationAttenuation;
    float _DeformationAmplification;
    uint _DeformationPatchFlags;
};

layout(set = WATER_DATA_SET, binding = 4, rgba16f) uniform image2D SurfaceGradientBuffer[4];

layout(location = 0) in struct {
    vec3 positionRWS;
    vec3 positionORWS;
    vec3 normal;
} fs_in;

layout(location = 3) noperspective in vec3 dist;

layout(location = 0) out vec4 fragColor;

struct BSDFData {
    vec3 diffuseColor;
    float roughness;
    vec3 normalWS;
    vec3 reflectedWS;
    float reflectionAttenuation;
    vec3 viewWS;
};

float saturate(float value);
REAL3_DP evaluate_normalized_planet_space(REAL3_DP positionPS, REAL_DP planetRadius);
REAL2_DP project_position_to_disk(REAL3_DP posNPS);
mat3 get_local_frame(vec3 posNPS, vec2 uv);
vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist);
float sanitize_normal(inout vec3 normalWS, vec3 viewWS, vec3 positionRWS);
vec3 EvaluateNormal(REAL2_DP sampleUV, mat3 localFrame, float distanceToCamera, vec4 patchSize, uint patchFlags, bool attenuation);
float EvaluateRoughness(float distanceToCamera, vec4 patchSize, vec4 patchRoughness);
vec3 EvaluateSunLightColor(vec3 upVector, float elevation, vec3 sunDirection);
vec3 EvaluateDirectLighting(BSDFData bsdfData, float NdotV, vec3 lightDir, vec3 lightColor);
vec3 EvaluateIndirectLighting(BSDFData bsdfData, REAL3_DP positionPS, vec3 lightDir);
void AdjustReflectionVector(inout vec3 R, vec3 baseNormal, inout float attenuation);
vec3 evaluate_earth_lighting(REAL3_DP positionOPS, vec3 positionRWS, bool attenuation);

vec3 tone_map(vec3 radiance) {
    vec3 whitePoint = max(atmosphereInfo.whitePoint.xyz, vec3(1e-3));
    vec3 mapped = vec3(1.0) - exp(-max(radiance, vec3(0.0)) / whitePoint * atmosphereInfo.exposure);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}

void main() {
    REAL3_DP positionOPS = REAL3_DP(fs_in.positionORWS) + _CameraPosition - _PlanetCenter;

    vec3 lighting = evaluate_earth_lighting(positionOPS, fs_in.positionRWS, _DeformationAttenuation != 0);
    lighting = apply_wireframe(lighting, vec3(_WireFrameColor), float(_WireFrameSize), dist);

    lighting = tone_map(lighting);
    fragColor = vec4(lighting, 1.0);
}

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

REAL_DP sqrt_real(REAL_DP value) {
#ifdef LEB_DOUBLE
    return value < REAL_DP(1e-32) ? REAL_DP(0.0) : REAL_DP(1.0) / inversesqrt(value);
#else
    return sqrt(max(value, REAL_DP(0.0)));
#endif
}

REAL_DP acos_real(REAL_DP value) {
#ifdef LEB_DOUBLE
    REAL_DP y = abs(clamp(value, REAL_DP(-1.0), REAL_DP(1.0)));
    REAL_DP sqrtY = y != REAL_DP(1.0) ? sqrt_real(REAL_DP(1.0) - y) : REAL_DP(0.0);
    REAL_DP z = (REAL_DP(-0.168577) * y + REAL_DP(1.56723)) * sqrtY;
    return value > REAL_DP(0.0) ? z : REAL_DP(0.5 * PI);
#else
    return acos(clamp(value, REAL_DP(-1.0), REAL_DP(1.0)));
#endif
}

REAL2_DP frac_real2(REAL2_DP value) {
    return value - floor(value);
}

REAL3_DP evaluate_normalized_planet_space(REAL3_DP positionPS, REAL_DP planetRadius) {
    REAL3_DP posNPS = positionPS / planetRadius;
    return posNPS.y == REAL_DP(1.0) ? REAL3_DP(0.0, 1.0, 0.0) : posNPS * inversesqrt(dot(posNPS, posNPS));
}

REAL2_DP project_position_to_disk(REAL3_DP posNPS) {
    REAL_DP v = abs(posNPS.y) + REAL_DP(1e-10);
    if (v <= REAL_DP(0.999)) {
        REAL_DP r = acos_real(v) / REAL_DP(0.5 * PI);
        REAL_DP s = posNPS.x * posNPS.x + posNPS.z * posNPS.z;
        REAL_DP p = s != REAL_DP(0.0) ? inversesqrt(s) * r : REAL_DP(0.0);
        return REAL2_DP(posNPS.x * p, posNPS.z * p);
    }
    return posNPS.xz / REAL_DP(0.5 * PI);
}

mat3 get_local_frame(vec3 posNPS, vec2 uv) {
    float u2 = uv.x * uv.x;
    float v2 = uv.y * uv.y;
    float u2v2 = u2 + v2;
    float sqrtU2V2 = sqrt(u2v2);

    if (sqrtU2V2 < 1e-7) {
        return mat3(vec3(1.0, 0.0, 0.0), posNPS, vec3(0.0, 0.0, 1.0));
    }

    float u2v2_32 = sqrtU2V2 * u2v2;
    float t = 0.5 * PI * sqrtU2V2;
    float b = t < 1e-5 ? t : sin(t);
    float a = t < 1e-5 ? 1.0 - t : cos(t);

    float tuX = PI * u2 * a / (2.0 * u2v2) + v2 * b / u2v2_32;
    float tuY = -PI * uv.x * b / (2.0 * sqrtU2V2);
    float tuZ = 0.5 * uv.x * uv.y * (PI * a / u2v2 - 2.0 * b / u2v2_32);

    float btvX = 0.5 * uv.x * uv.y * (PI * a / u2v2 - 2.0 * b / u2v2_32);
    float btvY = -PI * uv.y * b / (2.0 * sqrtU2V2);
    float btvZ = PI * v2 * a / (2.0 * u2v2) + u2 * b / u2v2_32;

    vec3 tang = abs(uv.x) >= 1e-7 ? normalize(vec3(tuX, tuY, tuZ)) : vec3(1.0, 0.0, 0.0);
    vec3 bitang = abs(uv.y) >= 1e-7 ? normalize(vec3(btvX, btvY, btvZ)) : vec3(0.0, 0.0, 1.0);

    if (posNPS.y < 0.0) {
        tang.y = -tang.y;
        bitang.y = -bitang.y;
    }

    return mat3(tang, posNPS, bitang);
}

ivec2 repeat_coord(ivec2 tap, uint width, uint height) {
    return ivec2(uint(tap.x) % width, uint(tap.y) % height);
}

vec3 load_surface_gradient_bilinear(uint bandIdx, vec2 uv) {
    int imageIdx = int(bandIdx);
    ivec2 textureSize = imageSize(SurfaceGradientBuffer[imageIdx]);
    vec2 unnormalized = fract(uv) * vec2(textureSize) - vec2(0.5);

    ivec2 tapCoord = ivec2(floor(floor(unnormalized) + vec2(0.5)));
    ivec2 p0Coord = repeat_coord(tapCoord, uint(textureSize.x), uint(textureSize.y));
    ivec2 p1Coord = repeat_coord(tapCoord + ivec2(1, 0), uint(textureSize.x), uint(textureSize.y));
    ivec2 p2Coord = repeat_coord(tapCoord + ivec2(0, 1), uint(textureSize.x), uint(textureSize.y));
    ivec2 p3Coord = repeat_coord(tapCoord + ivec2(1, 1), uint(textureSize.x), uint(textureSize.y));

    vec3 p0 = imageLoad(SurfaceGradientBuffer[imageIdx], p0Coord).xyz;
    vec3 p1 = imageLoad(SurfaceGradientBuffer[imageIdx], p1Coord).xyz;
    vec3 p2 = imageLoad(SurfaceGradientBuffer[imageIdx], p2Coord).xyz;
    vec3 p3 = imageLoad(SurfaceGradientBuffer[imageIdx], p3Coord).xyz;

    vec2 fraction = fract(unnormalized);
    vec3 i0 = mix(p0, p1, fraction.x);
    vec3 i1 = mix(p2, p3, fraction.x);
    return mix(i0, i1, fraction.y);
}

vec3 EvaluateNormal(REAL2_DP sampleUV, mat3 localFrame, float distanceToCamera, vec4 patchSize, uint patchFlags, bool attenuation) {
    vec3 totalSG = vec3(0.0);
    float activeBandCount = 0.0;
    float totalAtt = 0.0;

    for (uint bandIdx = 0u; bandIdx < NUM_SG_BANDS; ++bandIdx) {
        vec2 bandUV = vec2(frac_real2(sampleUV / REAL_DP(patchSize[bandIdx])));
        vec3 bandSG = load_surface_gradient_bilinear(bandIdx, bandUV);

        float att = attenuation
            ? mix(1.0, 0.0, saturate((distanceToCamera - patchSize[bandIdx] * SG_BAND_ATTENUATION_START) /
                (patchSize[bandIdx] * SG_BAND_ATTENUATION_END)))
            : 1.0;
        att *= float((patchFlags >> bandIdx) & 0x1u);

        totalSG += bandSG * att;
        totalAtt += att;
        activeBandCount += att != 0.0 ? 1.0 : 0.0;
    }

    if (activeBandCount > 1.0) {
        totalSG /= max(totalAtt, 0.0000001);
    }

    vec3 norm = normalize(vec3(0.0, 1.0, 0.0) - totalSG);
    norm = localFrame * norm;
    return normalize(norm);
}

float EvaluateRoughness(float distanceToCamera, vec4 patchSize, vec4 patchRoughness) {
    float roughness = 0.0;
    for (uint bandIdx = 0u; bandIdx < NUM_SG_BANDS; ++bandIdx) {
        float roughnessFactor = mix(0.0, 1.0, saturate((distanceToCamera - patchSize[bandIdx] * BAND_ROUGHNESS_START) /
            (patchSize[bandIdx] * BAND_ROUGHNESS_END)));
        roughness += roughnessFactor * patchRoughness[bandIdx];
    }
    return roughness;
}

float ray_sphere_intersect_nearest(vec3 r0, vec3 rd, vec3 s0, float sR) {
    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sR * sR);
    float delta = b * b - 4.0 * a * c;
    if (delta < 0.0 || a == 0.0) {
        return -1.0;
    }
    float sol0 = (-b - sqrt(delta)) / (2.0 * a);
    float sol1 = (-b + sqrt(delta)) / (2.0 * a);
    if (sol0 < 0.0 && sol1 < 0.0) {
        return -1.0;
    }
    if (sol0 < 0.0) {
        return max(0.0, sol1);
    }
    if (sol1 < 0.0) {
        return max(0.0, sol0);
    }
    return max(0.0, min(sol0, sol1));
}

vec3 EvaluateSunLightColor(vec3 upVector, float elevation, vec3 sunDirection) {
    float r = ClampRadius(ATMOSPHERE, localUnitsToAtmosphere(elevation));
    float earthShadow = dot(upVector, sunDirection) < 0.0
        ? (ray_sphere_intersect_nearest(upVector * r, sunDirection, vec3(0.0), ATMOSPHERE.bottom_radius) != -1.0 ? 0.0 : 1.0)
        : 1.0;
    float viewZenithCosAngle = dot(sunDirection, upVector);
    return GetTransmittanceToSun(ATMOSPHERE, TRANSMITTANCE_TEXTURE, r, viewZenithCosAngle) * earthShadow;
}

float F_Schlick(float f0, float f90, float u) {
    float x = 1.0 - u;
    float x2 = x * x;
    float x5 = x * x2 * x2;
    return (f90 - f0) * x5 + f0;
}

float F_Schlick(float f0, float u) {
    return F_Schlick(f0, 1.0, u);
}

float Sq(float v) {
    return v * v;
}

float ComputeWrappedDiffuseLighting(float NdotL, float w) {
    return saturate((NdotL + w) / ((1.0 + w) * (1.0 + w)));
}

float DV_SmithJointGGX(float NdotH, float NdotL, float NdotV, float roughness, float partLambdaV) {
    float a2 = Sq(roughness);
    float s = (NdotH * a2 - NdotH) * NdotH + 1.0;

    float lambdaV = NdotL * partLambdaV;
    float lambdaL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);

    float d = a2 / max(s * s, 1.175494351e-38);
    float g = 1.0 / max(lambdaV + lambdaL, 1.175494351e-38);
    return (1.0 / PI) * 0.5 * d * g;
}

float SmithJointGGXPartLambdaVApprox(float NdotV, float roughness) {
    return NdotV * (1.0 - roughness) + roughness;
}

vec3 EvaluateBSDF(BSDFData bsdfData, float NdotV, vec3 lightDir) {
    float NdotL = dot(bsdfData.normalWS, lightDir);
    float LdotV = dot(lightDir, bsdfData.viewWS);
    float invLenLV = inversesqrt(max(2.0 * LdotV + 2.0, 0.0000001));
    float NdotH = saturate((NdotL + NdotV) * invLenLV);
    float LdotH = saturate(invLenLV * LdotV + invLenLV);
    float partLambdaV = SmithJointGGXPartLambdaVApprox(NdotV, bsdfData.roughness);
    float clampedNdotL = saturate(NdotL);
    float F = F_Schlick(0.02, LdotH);
    float NdotLWrappedDiffuseLowFrequency = ComputeWrappedDiffuseLighting(NdotL, 0.5);

    vec3 diffR = (1.0 - F) * mix(1.0, NdotLWrappedDiffuseLowFrequency, 1.0) * bsdfData.diffuseColor;
    vec3 specR = vec3(F * DV_SmithJointGGX(NdotH, abs(NdotL), NdotV, bsdfData.roughness, partLambdaV) * clampedNdotL);
    return diffR + specR;
}

vec3 EvaluateDirectLighting(BSDFData bsdfData, float NdotV, vec3 lightDir, vec3 lightColor) {
    return EvaluateBSDF(bsdfData, NdotV, lightDir) * lightColor;
}

vec3 EvaluateIndirectLighting(BSDFData bsdfData, REAL3_DP positionPS, vec3 lightDir) {
    vec3 reflectedWS = normalize(bsdfData.reflectedWS);
    vec3 H = normalize(reflectedWS + bsdfData.viewWS);
    float F_Ind = F_Schlick(0.02, clamp(dot(reflectedWS, H), 0.0, 1.0));

    vec3 transmittance;
    vec3 luminance = GetSkyRadiance(vec3(positionPS), reflectedWS, 0.0, lightDir, transmittance);
    return F_Ind * luminance * bsdfData.reflectionAttenuation * vec3(0.9, 0.9, 0.95);
}

void AdjustReflectionVector(inout vec3 R, vec3 baseNormal, inout float attenuation) {
    float RdotN = dot(R, baseNormal);
    if (RdotN < 0.0) {
        R = reflect(R, baseNormal);
        attenuation = 1.0;
    }
}

float sanitize_normal(inout vec3 normalWS, vec3 viewWS, vec3 positionRWS) {
    float NdotV = dot(normalWS, viewWS);
    if (NdotV < 0.0) {
        normalWS = reflect(normalWS, viewWS);
        NdotV = -NdotV;
    }
    return NdotV;
}

vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist) {
    if (wireframeSize > 0.0) {
        vec3 d2 = dist * dist;
        float nearest = min(min(d2.x, d2.y), d2.z);
        float f = exp2(-nearest / wireframeSize);
        color = mix(color, wireFrameColor, f);
    }
    return color;
}

vec3 evaluate_earth_lighting(REAL3_DP positionOPS, vec3 positionRWS, bool attenuation) {
    REAL3_DP posNPS = evaluate_normalized_planet_space(positionOPS, _PlanetRadius);
    REAL2_DP sampleNUV = project_position_to_disk(posNPS);
    mat3 localFrame = get_local_frame(vec3(posNPS), vec2(sampleNUV));
    REAL2_DP sampleUV = sampleNUV * _PlanetRadius;

    BSDFData bsdfData;
    float distanceToCamera = length(positionRWS);

    bsdfData.normalWS = EvaluateNormal(sampleUV, localFrame, distanceToCamera, _DeformationPatchSize, _DeformationPatchFlags, attenuation);
    vec3 targetNormal = normalize(vec3(bsdfData.normalWS.x, 0.0, bsdfData.normalWS.z));
    bsdfData.normalWS = mix(targetNormal, bsdfData.normalWS, saturate(abs(float(posNPS.y)) / 1e-7));
    bsdfData.roughness = EvaluateRoughness(distanceToCamera, _DeformationPatchSize, _DeformationPatchRoughness);
    bsdfData.viewWS = -positionRWS / max(distanceToCamera, 0.00001);

    float NdotV = sanitize_normal(bsdfData.normalWS, bsdfData.viewWS, positionRWS);
    vec3 planetUpVector = normalize(vec3(positionOPS));

    bsdfData.reflectedWS = reflect(-bsdfData.viewWS, bsdfData.normalWS);
    bsdfData.reflectionAttenuation = 1.0;
    AdjustReflectionVector(bsdfData.reflectedWS, planetUpVector, bsdfData.reflectionAttenuation);
    bsdfData.diffuseColor = vec3(0.0, 0.02, 0.04);

    REAL3_DP positionPS = REAL3_DP(positionRWS) + _CameraPosition - _PlanetCenter;
    vec3 sunDirection = normalize(atmosphereInfo.sunDirection.xyz);
    vec3 lightColor = EvaluateSunLightColor(planetUpVector, length(vec3(positionPS)), sunDirection);

    vec3 lighting = EvaluateDirectLighting(bsdfData, NdotV, sunDirection, lightColor);
    lighting += EvaluateIndirectLighting(bsdfData, positionPS, sunDirection);
    return lighting;
}
