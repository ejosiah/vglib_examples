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

    vec3 sheenColorFactor;
    float sheenRoughnessFactor;

    vec2 anisotropyRotation;
    float anisotropyStrength;
    int unlit;

    vec3 specularColor;
    float specularFactor;

    float iridescenceFactor;
    float iridescenceIor;
    float iridescenceThicknessMinimum; // nanometers
    float iridescenceThicknessMaximum; // nanometers;
};

struct NormalInfo {
    vec3 T;
    vec3 B;
    vec3 N;
    vec3 Ng;
    vec3 Ntex;
};

struct TextureInfo {
    vec2 offset;
    vec2 scale;
    float tScale; // normal scale or occulsion sstrength
    int index;
    int texCoord;
    float rotation;
};
#endif // GLTF_GLSL