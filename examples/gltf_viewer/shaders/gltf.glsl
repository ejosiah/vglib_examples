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

    float transmission;
    float ior;
    float thickness;
    float attenuationDistance;

    vec3 attenuationColor;
    float dispersion;

    float emissiveStrength;
    float clearCoatFactor;
    float clearCoatRoughnessFactor;
    int textureInfoOffset;
};

struct TextureInfo {
    int index;
    int texCoord;
};

struct ClearCoat {
    vec3 normal;
    vec3 f0;
    vec3 f90;
    float factor;
    float roughness;
};

struct NormalInfo {
    vec3 T;
    vec3 B;
    vec3 N;
};

ClearCoat newClearCoatInstance() {
    ClearCoat cc;
    cc.factor = 0;
    cc.roughness = 0;
    cc.f0 = vec3(0);
    cc.f90 = vec3(1);
    cc.normal = vec3(0);

    return cc;
}

#endif // GLTF_GLSL