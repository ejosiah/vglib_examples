#pragma once

#include <glm/glm.hpp>

#include <cinttypes>
#include <filesystem>
#include <vector>

struct CPUMesh {
    // Total number of elements
    uint32_t totalNumElements = 0;

    // Minimal depth of the mesh
    uint32_t minimalDepth = 0;

    // Bisector
    std::vector<uint64_t> heapIDArray;
    std::vector<glm::uvec3> neighborsArray;

    // Base positions
    std::vector<glm::vec3> basePoints;

    static CPUMesh load_cpu_mesh(const std::filesystem::path& meshPath, uint32_t cbtNumElements);
};

// Load the CPU mesh
