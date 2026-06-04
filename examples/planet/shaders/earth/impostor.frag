#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 3
#define ATMOSPHERE_LUT_SET 4
#include "atmosphere/bruneton_api.glsl"

#define WATER_DATA_SET 5
#define MILKYWAY_SET 6
#define MILKYWAY_NIGHT_LIGHT_SCALE 0.08

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

layout(set = MILKYWAY_SET, binding = 0) uniform sampler2D MilkyWayTexture;

layout(location = 0) out vec4 fragColor;

struct BSDFData {
    vec3 diffuseColor;
    float roughness;
    vec3 normalWS;
    vec3 reflectedWS;
    float reflectionAttenuation;
    vec3 viewWS;
};

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec3 get_ray_direction(vec2 positionNDC) {
    REAL4_DP positionCS = REAL4_DP(REAL2_DP(positionNDC) * REAL_DP(2.0) - REAL_DP(1.0), REAL_DP(0.5), REAL_DP(1.0));
    positionCS.y = -positionCS.y;

    REAL4_DP positionRWS = _InvViewProjectionMatrix * positionCS;
    REAL3_DP virtualPosition = positionRWS.xyz / positionRWS.w;
    return normalize(vec3(virtualPosition));
}

float evaluate_depth(vec3 positionRWS) {
    REAL4_DP positionCS = _ViewProjectionMatrix * REAL4_DP(positionRWS, REAL_DP(1.0));
    return clamp(float(positionCS.z / positionCS.w), 0.0, 0.999999);
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

vec2 direction_to_equirect_uv(vec3 direction) {
    direction = normalize(direction);
    float u = atan(direction.z, direction.x) * (1.0 / (2.0 * PI)) + 0.5;
    float v = asin(clamp(direction.y, -1.0, 1.0)) * (1.0 / PI) + 0.5;
    return vec2(u, v);
}

vec3 EvaluateSunLightColor(vec3 upVector, float elevation, vec3 sunDirection) {
    float r = ClampRadius(ATMOSPHERE, localUnitsToAtmosphere(elevation));
    float earthShadow = dot(upVector, sunDirection) < 0.0
        ? (ray_sphere_intersect_nearest(upVector * r, sunDirection, vec3(0.0), ATMOSPHERE.bottom_radius) != -1.0 ? 0.0 : 1.0)
        : 1.0;
    float viewZenithCosAngle = dot(sunDirection, upVector);
    vec3 sunLight = GetTransmittanceToSun(ATMOSPHERE, TRANSMITTANCE_TEXTURE, r, viewZenithCosAngle) * earthShadow;
    vec3 milkyway = texture(MilkyWayTexture, direction_to_equirect_uv(upVector)).rgb * MILKYWAY_NIGHT_LIGHT_SCALE;
    float directLight = max(max(sunLight.r, sunLight.g), sunLight.b);
    float darkness = 1.0 - saturate(directLight);
    return sunLight + milkyway * darkness;
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

vec3 tone_map(vec3 radiance) {
    vec3 whitePoint = max(atmosphereInfo.whitePoint.xyz, vec3(1e-3));
    vec3 mapped = vec3(1.0) - exp(-max(radiance, vec3(0.0)) / whitePoint * atmosphereInfo.exposure);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}

void main() {
    vec2 positionNDC = gl_FragCoord.xy / vec2(_ScreenSize);
    vec3 rayDirection = get_ray_direction(positionNDC);

    float planetIntersection = ray_sphere_intersect_nearest(vec3(_CameraPosition), rayDirection, vec3(_PlanetCenter), float(_PlanetRadius));
    if (planetIntersection == -1.0) {
        discard;
    }

    REAL3_DP positionWS = _CameraPosition + REAL3_DP(rayDirection) * REAL_DP(planetIntersection);
    REAL3_DP positionPS = positionWS - _PlanetCenter;
    vec3 positionRWS = vec3(positionWS - _CameraPosition);
    gl_FragDepth = evaluate_depth(positionRWS);

    BSDFData bsdfData;
    bsdfData.viewWS = -rayDirection;
    bsdfData.normalWS = normalize(vec3(positionPS));
    bsdfData.roughness = _DeformationPatchRoughness.x + _DeformationPatchRoughness.y + _DeformationPatchRoughness.z + _DeformationPatchRoughness.w;
    bsdfData.reflectedWS = reflect(-bsdfData.viewWS, bsdfData.normalWS);
    bsdfData.reflectionAttenuation = 1.0;
    bsdfData.diffuseColor = vec3(0.0, 0.02, 0.04);

    float NdotV = dot(bsdfData.viewWS, bsdfData.normalWS);
    vec3 sunDirection = normalize(atmosphereInfo.sunDirection.xyz);
    vec3 lightColor = EvaluateSunLightColor(bsdfData.normalWS, length(vec3(positionPS)), sunDirection);

    vec3 lighting = EvaluateDirectLighting(bsdfData, NdotV, sunDirection, lightColor);
    lighting += EvaluateIndirectLighting(bsdfData, positionPS, sunDirection);

    fragColor = vec4(tone_map(lighting), 1.0);
}
