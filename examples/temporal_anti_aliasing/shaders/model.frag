#version 460

#include "gltf.glsl"

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(set = 0, binding = 1) buffer GLTF_MATERIAL {
    Material materials[];
};

void main() {

}