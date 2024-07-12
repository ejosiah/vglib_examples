#pragma once

#include <VulkanDevice.h>
#include <VulkanBuffer.h>

class StagingBuffer {
public:
    StagingBuffer() = default;

    StagingBuffer(VulkanDevice* device, VkDeviceSize size);

    BufferRegion allocate(VkDeviceSize size);

private:
    VulkanBuffer _buffer;
    VkDeviceSize _offset;
};