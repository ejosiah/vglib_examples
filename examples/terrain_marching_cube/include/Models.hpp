#pragma once

#include "VulkanBuffer.h"
#include "Texture.h"
#include <AbstractCamera.hpp>
#include <glm/glm.hpp>
#include <array>

struct Counters {
    uint32_t block_id;
};

struct CameraInfo {
    glm::mat4 view_projection{1};
    glm::mat4 inverse_view_projection{1};
    glm::mat4 grid_to_world;
    Frustum frustum{};
    glm::vec3 position;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
};

struct BlockData {
    glm::vec3 aabb;
    uint32_t vertex_id;
    uint32_t voxel_id;
    uint vertex_count;
};

struct BlockVertex {
    glm::vec3 position;
    glm::vec2 normal;
    float ambient_occlusion;
};

struct GpuData {
    std::vector<VulkanBuffer> cameraInfo;
    std::vector<VulkanBuffer> vertices;
    std::vector<Texture> voxels;
    VulkanBuffer distanceToCamera;
    VulkanBuffer blockData;
    VulkanBuffer counters;
};