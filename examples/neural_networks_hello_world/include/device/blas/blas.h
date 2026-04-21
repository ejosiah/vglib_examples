#pragma once

#include "common.h"
#include "VulkanDevice.h"

namespace blas {
    struct shape {
        uint i{}, j{};
    };
    struct matrix {
        VulkanBuffer buffer;
        shape shape{};
    };

    void init(VulkanDevice& device);

    void dot_product(VkCommandBuffer commandBuffer, const matrix& a, const matrix& b, matrix& result);
    
    void transpose(VkCommandBuffer commandBuffer, const matrix& x, matrix& result);

    void sigmoid(VkCommandBuffer commandBuffer, const matrix& x, matrix& result);

    void sigmoid_prime(VkCommandBuffer commandBuffer, const matrix& x, matrix& result);

    void cost_derivative(VkCommandBuffer commandBuffer, const matrix& a, const matrix& y, matrix& result);

    void add(VkCommandBuffer commandBuffer, const matrix& a, const matrix& b, matrix& result);

    void shutdown();
}
