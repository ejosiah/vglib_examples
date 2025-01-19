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
#include "Animation.hpp"

struct Volume {
    enum class Type{ LEVEL_SET, FOG };
    struct Voxel{ glm::vec3 position{}; float value{}; };

    glm::mat4 worldToVoxelTransform{1};
    glm::mat4 voxelToWorldTransform{1};
    uint64_t numVoxels{};
    glm::ivec3 dim{1};
    float voxelSize{1};
    struct {
        glm::vec3 min{std::numeric_limits<float>::max()};
        glm::vec3 max{std::numeric_limits<float>::lowest()};
    } bounds;
    Type type{};

    std::vector<Voxel> voxels{};
    float background{};

    static std::map<std::string, Volume> loadFromVdb(const std::filesystem::path& path);

    std::vector<float> placeIn(glm::vec3 bmin, glm::vec3 bmax);
};

using VolumeSet = std::map<std::string, Volume>;