#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "openvdb/openvdb.h"
#include "openvdb/io/Stream.h"

#include <vector>
#include <filesystem>
#include <numeric>
#include <glm/gtc/epsilon.hpp>
#include <map>
#include <string>

struct Volume {
    enum class Type{ LEVEL_SET, FOG };

    glm::mat4 worldToVoxelTransform{1};
    glm::mat4 voxelToWorldTransform{1};
    uint64_t numVoxels{};
    glm::ivec3 dim{1};
    double voxelSize{1};
    struct {
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{std::numeric_limits<float>::lowest()};
    } bounds;
    Type type{};

    std::vector<float> data;

    static std::map<std::string, Volume> loadFromVdb(const std::filesystem::path& path);
};

using VolumeSet = std::map<std::string, Volume>;
