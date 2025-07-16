#ifndef RAY_TRACING_IN_WEEKEND_METAL_GLSL
#define RAY_TRACING_IN_WEEKEND_METAL_GLSL

#include "common.glsl"

struct Metal {
    vec3 albedo;
    float fuzz;
};

void compute_metal_bsdf(vec3 p, vec3 n, vec3 wo, Metal material, inout HitRecord hRec) {
    hRec.n = n;
    hRec.x = p;
    float fuzz = material.fuzz;
    hRec.wi = reflect(-wo, n) + fuzz * uniformSampleSphere(sampleVec2(hRec));
    hRec.wi *= sign(max(0, dot(n, hRec.wi)));
    hRec.attenuation = material.albedo;
}

#endif // RAY_TRACING_IN_WEEKEND_METAL_GLSL