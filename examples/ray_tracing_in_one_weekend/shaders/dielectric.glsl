#ifndef RAY_TRACING_IN_WEEKEND_DIELECTRIC_GLSL
#define RAY_TRACING_IN_WEEKEND_DIELECTRIC_GLSL

#include "common.glsl"

struct Dielectric {
    float ior;
};

void compute_dielectric_bsdf(vec3 p, vec3 n, vec3 wo, float ior, inout HitRecord hRec) {
    vec3 I = normalize(-wo);
    vec3 N = normalize(n);
    float cos0 = dot(-I, N);

    float n0 = 1; // coming from air
    float n1 = ior;

    float kr = fresnel(cos0, n0, n1);

    if(cos0 < 0) {
        swap(n0, n1);
        N *= -1;
    }

    vec3 wi;
    if(rand(hRec.rngState) < kr) {
        hRec.wi = reflect(I, N);
        hRec.x = offsetRayImpl(p, N);
    }else {
        hRec.wi = refract(I, N, n0/n1);
        hRec.x = offsetRayImpl(p, N);
    }

    hRec.n = N;
    hRec.attenuation = vec3(1);
}

#endif // RAY_TRACING_IN_WEEKEND_DIELECTRIC_GLSL