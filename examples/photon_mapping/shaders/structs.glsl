#ifndef STRUCTS_GLSL
#define STRUCTS_GLSL

#include "rt_constants.glsl"
#include "photon.glsl"

struct Light {
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 power;
    vec3 lowerCorner;
    vec3 upperCorner;
};

struct Ray{
    vec3 origin;
    vec3 direction;
    float tMin;
    float tMax;
};

struct RtParams {
    Ray ray;
    vec4 material;
    vec3 normal;
    vec3 position;
    vec3 color;
    ivec2 pixelId;
    uint rngState;
    uint objectType;
};

const int SIDE_LEFT = 0;
const int SIDE_RIGHT = 1;

struct PhotonGenParams {
    Ray bounceRay;
    vec3 position;
    vec3 reflectivity;
    uint rngState;
    bool diffuse;
};

struct HitRecord {
    mat3 TBN;
    vec3 position;
    int meshId;
    bool hit;
};

#endif // STRUCTS_GLSL