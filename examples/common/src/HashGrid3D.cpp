#include "HashGrid3D.hpp"



HashGrid3D::HashGrid3D(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, uint32_t size, float spacing, bool unbounded) {


}

HashGrid3D HashGrid3D::bounded(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, float spacing, glm::vec3 bmin,
                               glm::vec3 bmax) {

    bmin -= spacing;
    bmax += spacing;
    glm::ivec3 dim{((bmax - bmin)/spacing) };
    const auto gridSize = dim.x * dim.y * dim.z;
    return HashGrid3D(args, boundingSpheres, maxObjects, gridSize, spacing, false);
}

HashGrid3D HashGrid3D::unBounded(VulkanBuffer args, VulkanBuffer boundingSpheres, uint32_t maxObjects, float spacing) {
    const auto gridSize = alignedSize(maxObjects * 5, 8);
    return HashGrid3D(args, boundingSpheres, maxObjects, gridSize, spacing, true);
}


void HashGrid3D::init() {
    static constexpr VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
    cellIds_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * maxObjects_ * 8);
    attributes_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * maxObjects_ * 8);

    counts_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, sizeof(uint32_t) * (gridSize_ + 1));
    cellIndexArray_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(CellInfo) * gridSize_);
    cellIndexStaging_ = reserve(sizeof(CellInfo) * gridSize_);
    bitSet_ = reserve(sizeof(uint32_t) * glm::max(gridSize_, maxObjects_));
    compactIndices_ = reserve(sizeof(uint32_t) * (gridSize_ + 1));
    indices_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, maxObjects_ * sizeof(uint32_t) * 8);


    dispatchBuffer_ = device_->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memoryUsage, DispatchByteSize, "dispatch_cmd_buffer");
}


BufferRegion HashGrid3D::reserve(VkDeviceSize size) {
    size = alignedSize(size, device_->getLimits().minStorageBufferOffsetAlignment);
    assert(scratchPad_.offset + size <= scratchPad_.buffer.size);
    auto start = scratchPad_.offset;
    scratchPad_.offset += size;
    return { &scratchPad_.buffer, start, scratchPad_.offset };
}