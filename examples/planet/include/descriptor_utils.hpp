#pragma once

#include <VulkanBuffer.h>

#include <cstdint>

inline VkDescriptorBufferInfo descriptor_buffer_info(const VulkanBuffer& buffer) {
    return { buffer, 0, VK_WHOLE_SIZE };
}

inline void set_buffer_write(VkWriteDescriptorSet& write, uint32_t binding, VkDescriptorType descriptorType,
                             const VkDescriptorBufferInfo* bufferInfo, uint32_t descriptorCount = 1,
                             uint32_t arrayElement = 0) {
    write.dstBinding = binding;
    write.descriptorType = descriptorType;
    write.descriptorCount = descriptorCount;
    write.pBufferInfo = bufferInfo;
    write.dstArrayElement = arrayElement;
}

inline void set_image_write(VkWriteDescriptorSet& write, uint32_t binding, VkDescriptorType descriptorType,
                            const VkDescriptorImageInfo* imageInfo, uint32_t descriptorCount = 1,
                            uint32_t arrayElement = 0) {
    write.dstBinding = binding;
    write.descriptorType = descriptorType;
    write.descriptorCount = descriptorCount;
    write.pImageInfo = imageInfo;
    write.dstArrayElement = arrayElement;
}
