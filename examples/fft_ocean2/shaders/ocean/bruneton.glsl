#ifndef OCEAN_SHADING_BRUNETON_GLSL
#define OCEAN_SHADING_BRUNETON_GLSL

#include "shading.glsl"

Shading shadeBruneton(vec3 P, vec3 N, vec3 V, vec3 R, vec3 H, vec3 L ) {
    vec3 env = vec3(0);
    vec3 F = vec3(0);
    vec3 scatter = vec3(0);
    vec3 spec = vec3(0);
    vec3 Lsun = vec3(0);

    return Shading(scatter, spec, env, Lsun, F);
}

#endif // OCEAN_SHADING_BRUNETON_GLSL