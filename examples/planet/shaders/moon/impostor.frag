#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

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

#define MOON_MATERIAL_SET 3
#define MOON_MATERIAL_DISABLE_PLANET_CB
#include "material_common.glsl"

layout(location = 0) out vec4 fragColor;

struct BSDFData {
    vec3 diffuseColor;
    float roughness;
    vec3 normalWS;
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

vec2 normalized_coordinates_to_longlat(vec3 positionNPS) {
    float lat = asin(clamp(positionNPS.z, -1.0, 1.0)) / PI + 0.5;
    float lon = atan(positionNPS.y, positionNPS.x) / (2.0 * PI) + 0.5;
    return vec2(lon, lat);
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

float sq(float v) {
    return v * v;
}

float compute_wrapped_diffuse_lighting(float NdotL, float w) {
    return saturate((NdotL + w) / ((1.0 + w) * (1.0 + w)));
}

float dv_smith_joint_ggx(float NdotH, float NdotL, float NdotV, float roughness, float partLambdaV) {
    float a2 = sq(roughness);
    float s = (NdotH * a2 - NdotH) * NdotH + 1.0;

    float lambdaV = NdotL * partLambdaV;
    float lambdaL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);

    float d = a2 / max(s * s, 1.175494351e-38);
    float g = 1.0 / max(lambdaV + lambdaL, 1.175494351e-38);
    return (1.0 / PI) * 0.5 * d * g;
}

float smith_joint_ggx_part_lambda_v_approx(float NdotV, float roughness) {
    return NdotV * (1.0 - roughness) + roughness;
}

vec3 evaluate_bsdf(BSDFData bsdfData, float NdotV, vec3 lightDir) {
    float NdotL = dot(bsdfData.normalWS, lightDir);
    float LdotV = dot(lightDir, bsdfData.viewWS);
    float invLenLV = inversesqrt(max(2.0 * LdotV + 2.0, 0.0000001));
    float NdotH = saturate((NdotL + NdotV) * invLenLV);
    float LdotH = saturate(invLenLV * LdotV + invLenLV);
    float partLambdaV = smith_joint_ggx_part_lambda_v_approx(NdotV, bsdfData.roughness);
    float clampedNdotL = saturate(NdotL);
    float F = F_Schlick(0.02, LdotH);
    float diffuse = compute_wrapped_diffuse_lighting(NdotL, 0.5);

    vec3 diffR = (1.0 - F) * diffuse * bsdfData.diffuseColor;
    vec3 specR = vec3(F * dv_smith_joint_ggx(NdotH, abs(NdotL), NdotV, bsdfData.roughness, partLambdaV) * clampedNdotL);
    return diffR + specR;
}

vec3 tone_map(vec3 radiance) {
    vec3 mapped = radiance / (vec3(1.0) + radiance);
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

    vec3 positionNPS = normalize(vec3(positionPS));
    vec2 lonlat = normalized_coordinates_to_longlat(positionNPS);

    BSDFData bsdfData;
    bsdfData.viewWS = -rayDirection;
    bsdfData.normalWS = positionNPS;
    bsdfData.roughness = 0.8;
    bsdfData.diffuseColor = texture(MOON_REPEAT(AlbedoTexture), lonlat).xyz;

    vec3 elevationSG = texture(MOON_REPEAT(ElevationSGTexture), lonlat).xyz;
    bsdfData.normalWS = normalize(bsdfData.normalWS - elevationSG);

    float NdotV = dot(bsdfData.viewWS, bsdfData.normalWS);
    vec3 lightDir = normalize(atmosphereInfo.sunDirection.xyz);
    vec3 lighting = evaluate_bsdf(bsdfData, NdotV, lightDir);

    fragColor = vec4(max(tone_map(lighting), vec3(0.0)), 1.0);
}
