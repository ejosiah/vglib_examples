#ifndef GLTF_GLSL
#define GLTF_GLSL

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

struct Anisotropy {
    vec3 tangent;
    vec3 bitangent;
    float strength;
    bool enabled;
};

struct Specular{
    vec3 color;
    float factor;
};

struct NormalInfo {
    vec3 T;
    vec3 B;
    vec3 N;
    vec3 Ng;
    vec3 Ntex;
};

struct Iridescence {
    float factor;
    float ior;
    float thickness;
    bool enabled;
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

Anisotropy newAnisotropyInstance() {
    Anisotropy aniso;
    aniso.tangent = vec3(1, 0, 0);
    aniso.bitangent = vec3(0, 1, 0);
    aniso.strength = 1;
    bool enabled = false;

    return aniso;
}

Specular newSpecluarInstance() {
    Specular s;
    s.color = vec3(1);
    s.factor = 1;

    return s;
}

Iridescence newIridescencInstance() {
    Iridescence ir;
    ir.factor = 0;
    ir.ior = 1.3;
    ir.thickness = 400;

    return ir;
}

vec4 getBaseColor();
NormalInfo getNormalInfo();
vec3 getMRO();
vec3 getEmission();
float getTransmissionFactor();
float getThickness();
bool isNull(Material material);
ClearCoat getClearCoat();
Sheen getSheen();
Anisotropy getAnisotropy();
Specular getSpecular();
bool hasTanget();
bool hasNormal();
Iridescence getIridescence();
vec2 transformUV(TextureInfo textureInfo);


#endif // GLTF_GLSL