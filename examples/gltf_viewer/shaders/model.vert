#version 460

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

    int unlit;
};

layout(set = 0, binding = 0) buffer Materials {
    Material materials[];
};

void main(){

}