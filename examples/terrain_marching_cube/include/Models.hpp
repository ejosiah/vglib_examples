#pragma once

#include "VulkanBuffer.h"
#include "Texture.h"
#include <AbstractCamera.hpp>
#include <glm/glm.hpp>
#include <array>

struct Counters {
    int free_slots{};
    int eviction_id{};
    uint processed_block_add_id{};
    uint empty_block_add_id{};
    uint blocks{};
    int slots_used{};
    uint debug_id;
};

struct CameraInfo {
    glm::mat4 view_projection{1};
    glm::mat4 inverse_view_projection{1};
    glm::mat4 grid_to_world;
    Frustum frustum{};
    glm::vec3 position;
    glm::vec3 aabbMin;
    glm::vec3 aabbMax;
    glm::vec3 direction;
    float offset;
};

struct BlockData {
    glm::vec3 aabb{};
    uint32_t voxel_id{};
};

struct BlockVertex {
    glm::vec3 position;
    glm::vec3 normal;
    float ambient_occlusion;
};

struct GpuData {
    std::vector<VulkanBuffer> cameraInfo;
    std::vector<VulkanBuffer> vertices;
    std::vector<Texture> voxels;
    std::array<VulkanBuffer, 2> distanceToCamera;
    std::array<Texture, 3> noise;
    VulkanBuffer blockData;
    VulkanBuffer counters;
    VulkanBuffer processed_blocks;
    VulkanBuffer empty_blocks;
    VulkanBuffer dispatchIndirectBuffer;
    VulkanBuffer drawIndirectBuffer;
    VulkanBuffer edgeLUT;
    VulkanBuffer triangleLUT;
    VulkanBuffer debugBuffer;
};

struct DrawCommand {
    uint  vertexCount{};
    uint  instanceCount{};
    uint  firstVertex{};
    uint  firstInstance{};
    glm::vec4 aabb{};
    uint vertex_id{};
};

struct DebugData {
    glm::vec3 my_block;
    glm::vec3 their_block;
    float my_distance;
    float their_distance;
    int eviction_id;
    int evicted;
    int skipped;
    int vertex_count;
    int slot;
};