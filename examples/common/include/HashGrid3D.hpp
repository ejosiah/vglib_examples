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

    static HashGrid3D bounded(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, float spacing, glm::vec3 bmin, glm::vec3 bmax);

    static HashGrid3D unBounded(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, float spacing);

    void init();


protected:
    uint32_t maxObjects_{};
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
    VulkanDevice* device_;
    bool unbounded_{};
    struct {
        VulkanBuffer distance;
    } constraints;
    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
    const float defaultRadius{0.06};
    uint32_t gridSize_{};

private:
    HashGrid3D(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, uint32_t size, float spacing, bool unbounded);

    struct Attribute {
        uint32_t objectID;
        uint32_t controlBits;
    };

    struct CellInfo {
        uint32_t index;
        uint32_t numHomeCells;
        uint32_t numPhantomCells;
        uint32_t numCells;
    };

    struct ScratchPad {
        VulkanBuffer buffer;
        VkDeviceSize offset{0};
    };

    static constexpr auto DispatchSize = sizeof(VkDispatchIndirectCommand);
    static constexpr uint32_t Object = 0;
    static constexpr uint32_t CellID = 1;
    static constexpr uint32_t CellArrayIndex = 2;
    static constexpr uint32_t Count = 3;

    static constexpr uint32_t ObjectCmd = 0;
    static constexpr uint32_t CellIDCmd = DispatchSize;
    static constexpr uint32_t CellArrayIndexCmd = DispatchSize * 2;
    static constexpr VkDeviceSize DispatchByteSize = DispatchSize * Count;

    ScratchPad scratchPad_;

    BufferRegion reserve(VkDeviceSize size);
};