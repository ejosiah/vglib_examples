#pragma once

#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/Exporter.hpp>
#include <assimp/postprocess.h>
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include "Mesh.h"

struct VertexOut {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    glm::vec2 uv;
};

void add_tangent_space_vectors(const std::filesystem::path& ipath, const std::filesystem::path& opath) {

    std::vector<mesh::Mesh> meshes{};
    std::vector<VertexOut> vertices{};
    mesh::load(meshes, ipath.string());
    for(const auto& mesh : meshes) {
        for(auto i : mesh.indices){
            const auto& vertex = mesh.vertices[i];
            VertexOut vOut{};
            vOut.position = vertex.position.xyz();
            vOut.normal = vertex.normal;
            vOut.tangent = vertex.tangent;
            vOut.bitangent = vertex.bitangent;
////            vOut.color = vertex.color;
            vOut.uv = vertex.uv * glm::vec2(1, -1);
            vertices.push_back(vOut);
        }
    }

    auto size = sizeof(VertexOut) * vertices.size();
    std::ofstream fout{opath.string(), std::ios::binary};
    if(!fout.good()) {
        fmt::print("unable to open file for writing");
        exit(120);
    }

    auto rawBytes = reinterpret_cast<char*>(vertices.data());
    fout.write(rawBytes, size);

    fmt::print("tangent space vectors added, output saved at {}\n", opath.string());
}