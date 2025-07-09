#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_scalar_block_layout : enable

#include "random.glsl"
#include "sampling.glsl"
#include "rtx_utils.glsl"

struct HitRecord {
    vec3 n;
    vec3 x;
    vec3 wo;
    vec3 wi;
    vec3 color;
    vec3 attenuation;
    RngStateType rngState;
    bool hit;
};


layout(set = 0, binding = 1, scalar) uniform Constants {
    mat4 viewInverse;
    mat4 projInverse;
    uint frame;
    uint maxBounce;
    uint sampleCount;
    uint currentSample;
    int adaptiveSampling;
};

vec2 sampleVec2(inout RngStateType rngState) {
    return vec2(rand(rngState), rand(rngState));
}

bool isBlack(vec3 v) {
    return all(equal(vec3(0), v));
}

float fresnelSchlick(float cosThata, float ior) {
    float r0 = (1 - ior)/(1+ior);
    r0 *= r0;
    return r0 + (1 - r0) * pow((1 - cosThata), 5);
}

void swap(inout float a, inout float b){
    float temp = a;
    a = b;
    b = temp;
}


float fresnel(float cosThetaI, float etaI, float etaT) {
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

void getSurfaceInfo(vec3 rOrigin, vec3 rDirection, float tHit, mat4x3 xform,  out vec3 position, out vec3 normal) {
    position = rOrigin + rDirection * tHit;
    vec3 center = xform * vec4(0, 0, 0, 1);
    normal = normalize(position - center);
}

#endif // COMMON_GLSL