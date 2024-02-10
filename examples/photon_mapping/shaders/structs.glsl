#ifndef STRUCTS_GLSL
#define STRUCTS_GLSL

#include "rt_constants.glsl"

struct Ray{
    vec3 origin;
    vec3 direction;
    float tMin;
    float tMax;
};

struct RtParams {
    Ray ray;
    vec3 color;
    uint rngState;
    uint objectType;
};

#endif // STRUCTS_GLSL