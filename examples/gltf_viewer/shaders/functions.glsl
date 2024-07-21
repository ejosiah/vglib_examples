#ifndef FUNCTIONS_GLTLS
#define FUNCTIONS_GLTLS

#include "octahedral.glsl"


float applyIorToRoughness(float roughness, float ior) {
    // Scale roughness with IOR so that an IOR of 1.0 results in no microfacet refraction and
    // an IOR of 1.5 results in the default amount of microfacet refraction.
    return roughness * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
}

vec2 dirToUV(vec3 dir) {
    return 0.5 + 0.5 * octEncode(normalize(dir));
}
float clampedDot(vec3 a, vec3 b) {
    return clamp(dot(a, b), 0, 1);
}

float max3(vec3 v){
    return max(v.x, max(v.y, v.z));
}

#endif // FUNCTIONS_GLTLS