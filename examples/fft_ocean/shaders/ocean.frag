#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "pbr/common.glsl"
#include "scene.glsl"

struct DebugInfo {
    vec4 N;
    vec4 V;
    vec4 R;
    ivec4 counters;
};

layout(set = 0, binding = 1) buffer DEBUG {
    DebugInfo debugInfo[];
};

layout(set = 2, binding = 10) uniform sampler2D global_textures[];
layout(set = 2, binding = 10) uniform sampler3D global_textures3d[];

#define GRADIENT_MAP (global_textures[scene.gradient_texture_id])

#define BLEND_START		8		// m
#define BLEND_END		200		// m

#define RADIANCE_API_ENABLED
#define BIND_LESS_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#include "atmosphere_api.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fs_in;

layout(location = 3) flat in int patchId;

layout(location = 0) out vec4 fragColor;

const vec3 deepBlue = vec3(0.0056f, 0.0194f, 0.0331f);
const vec3 carribbean = vec3(0.1812f, 0.4678f, 0.5520f);
const vec3 lightBlue = vec3(0.0000f, 0.2307f, 0.3613f);

const vec3 oceanColor = vec3(deepBlue);

vec3 hash31(float p){
    vec3 p3 = fract(vec3(p) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xxy+p3.yzz)*p3.zyx);
}

void swap(inout float a, inout float b){
    float temp = a;
    a = b;
    b = temp;
}

float fresnelDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    // Potentially swap indices of refraction
    bool entering = cosThetaI > 0.f;
    if (!entering) {
        swap(etaI, etaT);
        cosThetaI = abs(cosThetaI);
    }

    // Compute _cosThetaT_ using Snell's law
    float sinThetaI = sqrt(max(0, 1 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;

    // Handle total internal reflection
    if (sinThetaT >= 1) {
        return 1;
    }
    float cosThetaT = sqrt(max(0, 1 - sinThetaT * sinThetaT));
    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
    ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
    ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (Rparl * Rparl + Rperp * Rperp) / 2;
}

float fresnel(float cosTheta, float etaI, float etaT) {
    //return fresnelSchlick(cosTheta, f0(etaI, etaT));
    return fresnelDielectric(cosTheta, etaI, etaT);
}

const float preventDivideByZero = 0.0001;

vec3 getRadiance(vec3 pos, vec3 N, vec3 L) {
    vec3 sky_irradiance;
    vec3 sun_irradiance = GetSunAndSkyIrradiance(fs_in.worldPos - scene.earthCenter, N, scene.sunDirection, sky_irradiance);
    return max(0, dot(N, L)) * (sky_irradiance + sun_irradiance)/PI;
}

vec3 getRadianceIndirect(vec3 pos, vec3 N, vec3 L) {
    vec3 sky_irradiance;
    vec3 sun_irradiance = GetSunAndSkyIrradiance(fs_in.worldPos - scene.earthCenter, N, scene.sunDirection, sky_irradiance);
    return max(0, dot(N, L)) * (sky_irradiance)/PI;
}

const float iorAir = 1.0;
const float IorWater = 1.333;

void main(){
    fragColor = vec4(1);

    vec3 F0 = vec3(0.02);

    vec4 grad = texture(GRADIENT_MAP, fs_in.uv);

    vec3 viewDir = scene.camera - fs_in.worldPos;

    vec3 N = normalize(grad.xzy);
//    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(scene.sunDirection);
    vec3 E = normalize(viewDir);
    vec3 H = normalize(E + N);

    float cos0 = dot(E, H);
    vec3 F = fresnelSchlick(max(dot(N,E), 0), F0);
    vec3 kD = 1 - F;
    vec3 kS = F;
//    if(cos0 < 0) {
//        N *= -1;
//    }
    vec3 R = reflect(-E, N);

//    if(R.y < 0) {
//        R *= -1;
//    }

    vec3 transmittance;
    vec3 reflection = GetSkyRadiance(fs_in.worldPos - scene.earthCenter, R, 0, scene.sunDirection, transmittance);
    if (dot(R, scene.sunDirection) > scene.sunSize.y) {
        reflection = reflection + transmittance * GetSolarRadiance();
    }

    float dist = length(viewDir.xz);
    float factor = (BLEND_END - dist) / (BLEND_END - BLEND_START);
    vec2 perl = vec2(0.0);

    factor = clamp(factor * factor * factor, 0.0, 1.0);

    float turbulence = max(1.6 - grad.w, 0.0);
    float color_mod = 1.0 + 3.0 * smoothstep(1.2, 1.8, turbulence);

    color_mod = mix(1.0, color_mod, factor);


    float NdotL = max(dot(N, L), 0.0);

    vec3 diffuse = (kD * oceanColor) * getRadiance(fs_in.worldPos, N, L);
    vec3 ambient = oceanColor * 0.02;
    vec3 specular = reflection * kS;

    uint NSamples = 100;
    vec3 indirect = vec3(0);
    for(uint i = 0; i < NSamples; ++i) {
        vec2 Xi = hammersley(i, NSamples);
        vec3 iL = importanceSampleGGX(Xi, N, 0);
        indirect += getRadianceIndirect(fs_in.worldPos, N, iL);
    }
    indirect /= float(NSamples);

    vec3 ocean_radiance = specular + diffuse; // + indirect

    vec3 in_scatter = GetSkyRadianceToPoint(scene.camera - scene.earthCenter, fs_in.worldPos - scene.earthCenter, 0, scene.sunDirection, transmittance);

    vec3 color = ocean_radiance;

    int i = atomicAdd(debugInfo[0].counters.x, 1);
    debugInfo[i].N.xyz = N;
    debugInfo[i].V.xyz = E;
    debugInfo[i].R.xyz = R;

//    color = hash31(float(patchId + 1));
    fragColor.rgb = pow(vec3(1.0) - exp(-color / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
    if( R.y < 0 ) {
        debugInfo[i].counters.y = 1;
    }
}