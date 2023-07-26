#ifndef MEDIUM_GLSL
#define MEDIUM_GLSL

#include "constants.glsl"
#include "util.glsl"

struct Medium{
    vec3 ac;    // absorption coefficient
    vec3 sc;    // scattering coefficient
    float g;
};

float phaseHG(float cos0, float g){
    float denom = 1 + g * g + 2 * g * cos0;
    return INV_4PI * (1 - g * g) / (denom * sqrt(denom));
}

float HG_p(float g, vec3 wo, vec3 wi){
    return phaseHG(dot(wo, wi), g);
}

float HG_sample_P(float g, vec3 wo, out vec3 wi, vec2 u){
    float cos0;
    if(abs(g) < 0.001){
        cos0 = 1 - 2 * u.x;
    }else {
        float sqrTerm = (1 - g * g) / (1 - g + 2 * g * u.x);
        cos0 = (1 + g * g - sqrTerm * sqrTerm) / (2 * g);
    }

    float sin0 = sqrt(max(0, 1 - cos0 * cos0));
    float phi = 2 * PI * u.y;
    vec3 v1, v2;
    othonormalBasis(v1, v2, wo);
    wi = sphericalDirection(sin0, cos0, phi, v1, v2, -wo);
    return phaseHG(-cos0, g);
}

#endif // MEDIUM_GLSL