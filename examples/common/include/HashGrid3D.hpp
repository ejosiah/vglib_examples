#pragma once

#include <VulkanDevice.h>

#include <array>

/*
 * GPU input args
 * layout(set = ?, binding = ?, scalar) buffer Args {
 *  Domain domain;
 *  uint numObjects;
 *
 * } args
 *
 */

class HashGrid3D {
public:
    HashGrid3D() = default;

    static HashGrid3D bounded(int maxObjects, VulkanBuffer args, VulkanBuffer boundingBoxes, float spacing, glm::ivec3 bmin, glm::ivec3 bmax);

    static HashGrid3D unBounded(int maxObjects, VulkanBuffer args, VulkanBuffer boundingBoxes, float spacing);


protected:
    int maxObjects_;
    VulkanBuffer indices_;
    VulkanBuffer boundingBox_;
    VulkanBuffer cellIds_;
    VulkanBuffer counts_;
    VulkanBuffer attributes_;
    VulkanBuffer cellIndexArray_;
    BufferRegion cellIndexStaging_;
    BufferRegion bitSet_;
    BufferRegion compactIndices_;
    VulkanBuffer dispatchBuffer_;
    bool unbounded_{};
    struct {
        VulkanBuffer distance;
    } constraints;
    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
    const float defaultRadius{0.06};
    uint32_t gridSize{};

private:
    HashGrid3D(int maxObjects, VulkanBuffer args, VulkanBuffer boundingBoxes);
};