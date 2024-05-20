#pragma once

#include "VulkanBuffer.h"
#include <glm/glm.hpp>
#include <cinttypes>


struct Geometry {
    VulkanBuffer vertices;
    VulkanBuffer indices;
    VulkanBuffer uboBuffer;
    float radius = 1;
    uint32_t indexCount;

    struct {
        glm::mat4 worldSpaceXform = glm::mat4(1);
        glm::mat4 localSpaceXform = glm::mat4(1);
        glm::vec3 center;
        float radius;
    } ubo;

    void bindVertexBuffers(VkCommandBuffer commandBuffer) {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, indices, 0, VK_INDEX_TYPE_UINT32);
    }
};