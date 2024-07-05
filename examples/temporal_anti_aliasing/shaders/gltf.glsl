#ifndef GLTF_GLSL
#define GLTF_GLSL

#include "gltf_brdf.glsl"

#define ALPHA_MODE_OPAQUE 0
#define ALPHA_MODE_MASK 1
#define ALPHA_MODE_BLEND 2

struct Mesh {
    mat4 model;
    mat4 model_inverse;
    int materialId;
};

struct Material {
    vec4 baseColor;

    vec3 emission;
    float alphaCutOff;

    float metalness;
    float roughness;
    int alphaMode;
    int doubleSided;

    ivec4 textures0;
    ivec4 textures1;
};

struct BrdfInfo {
    vec3 V; // view vector
    vec3 L; // light vector
    vec3 N; // normal
    vec3 H; // halfway vector
    float NdotL;
    float NdotV;
    float NdotH;
    float LdotH;
    float VdotH;
};

#endif // GLTF_GLSL