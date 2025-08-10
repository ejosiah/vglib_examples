#include "common.h"
#include "primitives.h"
#include "SDKmesh.h"
#include "Mesh.h"

#include <span>

int main() {
    fmt::print("Hello World!\n");

    auto input = R"(C:\Users\joebh\Downloads\iryoku-separable-sss-v1.0-0-gb217468\iryoku-separable-sss-245d073\Demo\Media\Head\Head.sdkmesh)";

    auto bytes = loadFile(input);

    auto header = reinterpret_cast<SDKMESH_HEADER*>(bytes.data());

    auto indexHeader = reinterpret_cast<SDKMESH_INDEX_BUFFER_HEADER*>(bytes.data() + header->IndexStreamHeadersOffset);
    auto vertexHeader = reinterpret_cast<SDKMESH_VERTEX_BUFFER_HEADER*>(bytes.data() + header->VertexStreamHeadersOffset);

    std::span<uint16_t> indices{ reinterpret_cast<uint16_t*>(bytes.data() + indexHeader->DataOffset), size_t{indexHeader->NumIndices} };
    std::span<SDKMESH_VERTEX> vertices{ reinterpret_cast<SDKMESH_VERTEX*>(bytes.data() + vertexHeader->DataOffset), size_t{vertexHeader->NumVertices} };

    std::span<SDKMESH_MATERIAL> materials{ reinterpret_cast<SDKMESH_MATERIAL*>(bytes.data() + header->MaterialDataOffset), to<size_t>(header->NumMaterials) };
    std::span<SDKMESH_MESH> meshes{ reinterpret_cast<SDKMESH_MESH*>(bytes.data() + header->MeshDataOffset), to<size_t>(header->NumMeshes) };
    std::span<SDKMESH_FRAME> frames{ reinterpret_cast<SDKMESH_FRAME*>(bytes.data() + header->FrameDataOffset), to<size_t>(header->NumFrames) };
    auto vertexSize = sizeof(SDKMESH_VERTEX);
    fmt::print("version: {}\n", header->Version);

    mesh::Mesh mesh{};
    mesh.name = "head";
    mesh.vertices = map_range(vertices, [](const SDKMESH_VERTEX& v){
        Vertex vv{};
        vv.position = glm::vec4(v.position, 1);
        vv.normal = v.normal;
        vv.tangent = v.tangent;
        vv.uv = v.texCoord;
        return vv;
    });
    mesh.indices = map_range(indices, [](const auto i){ return to<uint>(i); });
    mesh.textureMaterial.diffuseMap = materials.front().DiffuseTexture;
    mesh.textureMaterial.normalMap = materials.front().NormalTexture;

    std::vector<mesh::Mesh> objMeshes{ mesh };
    mesh::writeToObject(objMeshes, "head_skdmesh");

}