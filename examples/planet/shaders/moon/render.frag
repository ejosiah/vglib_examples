#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

layout(set = 1, binding = 1, scalar) readonly buffer PlanetCB {
    REAL3_DP _PlanetCenter;
    REAL_DP _PlanetRadius;
};

#define MOON_MATERIAL_SET 2
#define MOON_MATERIAL_DISABLE_PLANET_CB
#include "material_common.glsl"
#include "../transforms.glsl"

#define SLOPE_BAND_ATTENUATION_START 20.0
#define SLOPE_BAND_ATTENUATION_END 25.0
#define BAND_GROUP_SIZE_FACTOR 16.0
#define SLOPE_COMPENSATION_FACTOR 1.0

layout(location = 0) in struct {
    vec3 positionRWS;
    vec3 positionORWS;
} fs_in;

layout(location = 2) noperspective in vec3 dist;

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

REAL_DP asin_real(REAL_DP value) {
#ifdef LEB_DOUBLE
    REAL_DP negate = value < REAL_DP(0.0) ? REAL_DP(1.0) : REAL_DP(0.0);
    REAL_DP x = abs(clamp(value, REAL_DP(-1.0), REAL_DP(1.0)));
    REAL_DP ret = REAL_DP(-0.0187293);
    ret = ret * x + REAL_DP(0.0742610);
    ret = ret * x - REAL_DP(0.2121144);
    ret = ret * x + REAL_DP(1.5707288);
    ret = REAL_DP(0.5 * PI) - sqrt_real(REAL_DP(1.0) - x) * ret;
    return ret - REAL_DP(2.0) * negate * ret;
#else
    return asin(clamp(value, REAL_DP(-1.0), REAL_DP(1.0)));
#endif
}

REAL_DP atan2_real(REAL_DP y, REAL_DP x) {
#ifdef LEB_DOUBLE
    REAL_DP ax = abs(x);
    REAL_DP ay = abs(y);
    REAL_DP t0 = max(ax, ay);
    REAL_DP t1 = min(ax, ay);

    if (t0 == REAL_DP(0.0)) {
        return REAL_DP(0.0);
    }

    REAL_DP a = t1 / t0;
    REAL_DP s = a * a;
    REAL_DP p = REAL_DP(1.858552116405489677124095112269935093498e-3);
    p = p * s + REAL_DP(-1.079891788348568421355096111489189625479e-2);
    p = p * s + REAL_DP(2.938542391751121307313459297120064977888e-2);
    p = p * s + REAL_DP(-5.205055255952184339031830383744136009889e-2);
    p = p * s + REAL_DP(7.212338962134411520637759523226823838487e-2);
    p = p * s + REAL_DP(-8.993611617787817334566922323958104463948e-2);
    p = p * s + REAL_DP(1.110012236849539584126568416131750076191e-1);
    p = p * s + REAL_DP(-1.428514132711481940637283859690014415584e-1);
    p = p * s + REAL_DP(1.999999117496509842004185053319506031014e-1);
    p = p * s + REAL_DP(-3.333333333333333333333333333303396520128e-1);
    p = p * s * a + a;

    REAL_DP r = ay > ax ? REAL_DP(0.5 * PI) - p : p;
    r = x < REAL_DP(0.0) ? REAL_DP(PI) - r : r;
    return y < REAL_DP(0.0) ? -r : r;
#else
    return atan(y, x);
#endif
}

REAL2_DP frac_real2(REAL2_DP value) {
    return value - floor(value);
}

REAL2_DP normalized_coordinates_to_longlat(REAL3_DP positionNPS) {
    REAL_DP lat = asin_real(positionNPS.z) / REAL_DP(PI) + REAL_DP(0.5);
    REAL_DP lon = atan2_real(positionNPS.y, positionNPS.x) / REAL_DP(2.0 * PI) + REAL_DP(0.5);
    return REAL2_DP(lon, lat);
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

void combine_band_slopes(float distanceToCamera, vec2 slope, uint attenuation, inout float waveLength, inout float amplitude, inout vec2 totalSlope) {
    slope = slope * amplitude / waveLength * 0.5;

    float att = attenuation != 0u
        ? mix(1.0, 0.0, saturate((distanceToCamera - waveLength * SLOPE_BAND_ATTENUATION_START) / (waveLength * SLOPE_BAND_ATTENUATION_END)))
        : 1.0;

    totalSlope += slope * att * SLOPE_COMPENSATION_FACTOR;
    waveLength *= 0.5;
    amplitude *= 0.5;
}

vec3 evaluate_detail_sg(REAL2_DP sampleUV, float patchSize, float patchAmplitude, uint numOctaves, mat3 localFrame, float distanceToCamera, uint attenuation) {
    vec2 totalSlope = vec2(0.0);
    float waveLength = patchSize;
    float amplitude = patchAmplitude;
    uint bandIdx = 0u;

    for (; bandIdx < min(2u, numOctaves); ++bandIdx) {
        REAL2_DP bandUV = frac_real2(sampleUV / REAL_DP(waveLength));
        vec2 slope = texture(MOON_REPEAT(DetailSGTexture), vec2(bandUV)).xy;
        combine_band_slopes(distanceToCamera, slope, attenuation, waveLength, amplitude, totalSlope);
    }

    waveLength /= BAND_GROUP_SIZE_FACTOR;
    amplitude /= BAND_GROUP_SIZE_FACTOR;

    for (; bandIdx < numOctaves; ++bandIdx) {
        REAL2_DP bandUV = frac_real2(sampleUV / REAL_DP(waveLength));
        vec2 slope = texture(MOON_REPEAT(DetailSGTexture), vec2(bandUV)).xy;
        combine_band_slopes(distanceToCamera, slope, attenuation, waveLength, amplitude, totalSlope);
    }

    vec3 normal = normalize(vec3(-totalSlope.x, 1.0, totalSlope.y));
    return normalize(localFrame * normal);
}

float sanitize_normal(inout vec3 normalWS, vec3 viewWS) {
    float NdotV = dot(normalWS, viewWS);
    if (NdotV < 0.0) {
        normalWS = reflect(normalWS, viewWS);
        NdotV = -NdotV;
    }
    return NdotV;
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
    vec3 mapped = radiance/(vec3(1.0) + radiance);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}


void main() {
    REAL3_DP positionOPS = REAL3_DP(fs_in.positionORWS) + _CameraPosition - _PlanetCenter;
    REAL3_DP posNPS = evaluate_normalized_planet_space(positionOPS, _PlanetRadius);
    REAL2_DP lonlat = normalized_coordinates_to_longlat(posNPS);
    REAL2_DP sampleNUV = project_position_to_disk(posNPS);
    REAL2_DP sampleUV = sampleNUV * _PlanetRadius;

    vec3 geometricNormal = vec3(posNPS);
    mat3 localFrame = get_local_frame(geometricNormal, vec2(sampleNUV));
    float distanceToCamera = length(fs_in.positionRWS);

    BSDFData bsdfData;
    bsdfData.diffuseColor = texture(MOON_REPEAT(AlbedoTexture), vec2(lonlat)).xyz;
    bsdfData.viewWS = -fs_in.positionRWS / max(distanceToCamera, 0.00001);
    bsdfData.roughness = 0.8;

    vec3 detailNormal = evaluate_detail_sg(sampleUV, _PatchSize, _PatchAmplitude, uint(_NumOctaves), localFrame, distanceToCamera, _Attenuation);
    vec3 detailSG = surface_gradient_from_perturbed_normal(geometricNormal, detailNormal);
    vec3 elevationSG = texture(MOON_REPEAT(ElevationSGTexture), vec2(lonlat)).xyz;

    bsdfData.normalWS = normalize(geometricNormal - elevationSG - detailSG);
    float NdotV = sanitize_normal(bsdfData.normalWS, bsdfData.viewWS);

    vec3 lightDir = normalize(vec3(_SunDirection));
    vec3 lighting = evaluate_bsdf(bsdfData, NdotV, lightDir);
    lighting = apply_wireframe(lighting, vec3(_WireFrameColor), float(_WireFrameSize), dist);
    lighting = tone_map(lighting);
    fragColor = vec4(max(lighting, vec3(0.0)), 1.0);
}
