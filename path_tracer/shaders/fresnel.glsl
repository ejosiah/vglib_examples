#ifndef FRESNEL_GLSL
#define FRESNEL_GLSL

#include "constants.glsl"
#include "util.glsl"

vec3 baseColorToSpecularF0(vec3 baseColor, float metalness) {
    return mix(vec3(MIN_DIELECTRICS_F0), baseColor, metalness);
}

vec3 baseColorToDiffuseReflectance(vec3 baseColor, float metalness){
    return baseColor * (1.0f - metalness);
}

vec3 evalFresnelSchlick(vec3 f0, float f90, float NdotS)
{
    return f0 + (f90 - f0) * pow(1.0f - NdotS, 5.0f);
}

vec3 evalFresnel(vec3 f0, float f90, float NdotS)
{
    return evalFresnelSchlick(f0, f90, NdotS);
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


// Attenuates F90 for very low F0 values
// Source: "An efficient and Physically Plausible Real-Time Shading Model" in ShaderX7 by Schuler
// Also see section "Overbright highlights" in Hoffman's 2010 "Crafting Physically Motivated Shading Models for Game Development" for discussion
// IMPORTANT: Note that when F0 is calculated using metalness, it's value is never less than MIN_DIELECTRICS_F0, and therefore,
// this adjustment has no effect. To be effective, F0 must be authored separately, or calculated in different way. See main text for discussion.
float shadowedF90(vec3 F0) {
    // This scaler value is somewhat arbitrary, Schuler used 60 in his article. In here, we derive it from MIN_DIELECTRICS_F0 so
    // that it takes effect for any reflectance lower than least reflective dielectrics
    //const float t = 60.0f;
    const float t = (1.0f / MIN_DIELECTRICS_F0);
    return min(1.0f, t * luminance(F0));
}

#endif // FRESNEL_GLSL