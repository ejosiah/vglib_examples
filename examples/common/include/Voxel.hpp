#pragma once

#include "VulkanBuffer.h"
#include "VulkanRAII.h"
#include <glm/glm.hpp>
#include "Texture.h"

struct VoxelData {
    glm::mat4 worldToVoxelTransform{1};
    glm::mat4 voxelToWordTransform{1};
    int numVoxels{};
};

struct Voxels {
    Texture texture;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet{};
    VkDescriptorSet transformsDescriptorSet{};
    glm::mat4 transform;
    VoxelData* data;
    VulkanBuffer dataBuffer;
    glm::ivec3 dim{1};
    float voxelSize{1};
    struct {
        alignas(16) glm::vec3 min{0};
        alignas(16) glm::vec3 max{1};
    } bounds;
};