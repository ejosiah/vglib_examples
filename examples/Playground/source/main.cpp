#include "common.h"
#include "primitives.h"
#include "SDKmesh.h"
#include "Mesh.h"

#include <span>

#include <glm/glm.hpp>
#include <fmt/core.h>
#include <numeric>

float GetTextureCoordFromUnitRange(float x, int texture_size) {
    return 0.5 / float(texture_size) + x * (1.0 - 1.0 / float(texture_size));
}

float GetUnitRangeFromTextureCoord(float u, int texture_size) {
    return (u - 0.5 / float(texture_size)) / (1.0 - 1.0 / float(texture_size));
}

float fromUnitToSubUvs(float u, float resolution) { return (u + 0.5f / resolution) * (resolution / (resolution + 1.0f)); }
float fromSubUvsToUnit(float u, float resolution) { return (u - 0.5f / resolution) * (resolution / (resolution - 1.0f)); }

static constexpr float TRANSMITTANCE_TEXTURE_WIDTH = 256;
static constexpr float TRANSMITTANCE_TEXTURE_HEIGHT = 64;

#define AP_SLICE_COUNT 32

#define AP_KM_PER_SLICE 4.0f

float AerialPerspectiveDepthToSlice(float depth){
    return depth * (1.0f / AP_KM_PER_SLICE);
}
float AerialPerspectiveSliceToDepth(float slice){
    return slice * AP_KM_PER_SLICE;
}

int main() {
    const auto N = 32;
    for(auto i = 0; i < N; ++i) {
        float slice = (static_cast<float>(i) + 0.5)/ AP_SLICE_COUNT;
        slice *= slice;
        slice *= AP_SLICE_COUNT;
        auto tMax = AerialPerspectiveSliceToDepth(slice);
        fmt::print("i: {}, slice: {} tMax: {}\n", i, slice, tMax);
    }
}

//int main() {
//    fmt::print("Hello World!\n");
//
//    auto input = R"(C:\Users\joebh\Downloads\iryoku-separable-sss-v1.0-0-gb217468\iryoku-separable-sss-245d073\Demo\Media\Head\Head.sdkmesh)";
//
//    auto bytes = loadFile(input);
//
//    auto header = reinterpret_cast<SDKMESH_HEADER*>(bytes.data());
//
//    auto indexHeader = reinterpret_cast<SDKMESH_INDEX_BUFFER_HEADER*>(bytes.data() + header->IndexStreamHeadersOffset);
//    auto vertexHeader = reinterpret_cast<SDKMESH_VERTEX_BUFFER_HEADER*>(bytes.data() + header->VertexStreamHeadersOffset);
//
//    std::span<uint16_t> indices{ reinterpret_cast<uint16_t*>(bytes.data() + indexHeader->DataOffset), size_t{indexHeader->NumIndices} };
//    std::span<SDKMESH_VERTEX> vertices{ reinterpret_cast<SDKMESH_VERTEX*>(bytes.data() + vertexHeader->DataOffset), size_t{vertexHeader->NumVertices} };
//
//    std::span<SDKMESH_MATERIAL> materials{ reinterpret_cast<SDKMESH_MATERIAL*>(bytes.data() + header->MaterialDataOffset), to<size_t>(header->NumMaterials) };
//    std::span<SDKMESH_MESH> meshes{ reinterpret_cast<SDKMESH_MESH*>(bytes.data() + header->MeshDataOffset), to<size_t>(header->NumMeshes) };
//    std::span<SDKMESH_FRAME> frames{ reinterpret_cast<SDKMESH_FRAME*>(bytes.data() + header->FrameDataOffset), to<size_t>(header->NumFrames) };
//    auto vertexSize = sizeof(SDKMESH_VERTEX);
//    fmt::print("version: {}\n", header->Version);
//
//    mesh::Mesh mesh{};
//    mesh.name = "head";
//    mesh.vertices = map_range(vertices, [](const SDKMESH_VERTEX& v){
//        Vertex vv{};
//        vv.position = glm::vec4(v.position, 1);
//        vv.normal = v.normal;
//        vv.tangent = v.tangent;
//        vv.uv = v.texCoord;
//        return vv;
//    });
//    mesh.indices = map_range(indices, [](const auto i){ return to<uint>(i); });
//    mesh.textureMaterial.diffuseMap = materials.front().DiffuseTexture;
//    mesh.textureMaterial.normalMap = materials.front().NormalTexture;
//
//    std::vector<mesh::Mesh> objMeshes{ mesh };
//    mesh::writeToObject(objMeshes, "head_skdmesh");
//
//}