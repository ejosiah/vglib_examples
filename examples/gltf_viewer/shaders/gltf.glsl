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
};

struct TextureInfo {
    vec2 offset;
    vec2 scale;
    float tScale; // normal scale or occulsion sstrength
    int index;
    int texCoord;
    float rotation;
};

struct ClearCoat {
    vec3 normal;
    vec3 f0;
    vec3 f90;
    float factor;
    float roughness;
    bool enabled;
};

struct Sheen {
    vec3 color;
    float roughness;
    bool enabled;
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
    cc.enabled = false;

    return cc;
}

Sheen newSheenInstance() {
    Sheen s;
    s.color = vec3(0);
    s.roughness = 0;
    s.enabled = false;

    return s;
}

vec4 getBaseColor();
vec3 getNormal();
vec3 getMRO();
bool noTangets();
vec3 getEmission();
float getTransmissionFactor();
float getThickness();
bool isNull(Material material);
ClearCoat getClearCoat();
Sheen getSheen();
vec2 transformUV(TextureInfo textureInfo);


#endif // GLTF_GLSL