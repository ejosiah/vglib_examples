#version 460

struct Material {
    vec4 baseColor;

    vec3 emission;
    float alphaCutOff;

    float metalness;
    float roughness;
    int alphaMode;
    int doubleSided;

    uvec4 textures0;
    uvec4 textures1;
};
layout(set = 0, binding = 0) buffer MeshData {
    Material materials[];
};

void main() {

}