#pragma once

#include "VulkanDevice.h"

class AppContext {
public:
    static void init(VulkanDevice& device, VulkanDescriptorPool& descriptorPool);

    static VulkanDevice& device();

    static VulkanDescriptorPool& descriptorPool();

    static void bindInstanceDescriptorSets(VkCommandBuffer commandBuffer, VulkanPipelineLayout& layout);

    static void renderClipSpaceQuad(VkCommandBuffer commandBuffer);

    static VulkanDescriptorSetLayout & instanceSetLayout();

    static VkDescriptorSet allocateInstanceDescriptorSet();

    static void shutdown();

private:
    static void createDescriptorSets();

    static void updateDescriptorSets();

    void init0();

private:
    AppContext() = default;

    AppContext(VulkanDevice& device, VulkanDescriptorPool& descriptorPool);


    VulkanDevice* _device;
    VulkanDescriptorPool* _descriptorPool;
    VulkanDescriptorSetLayout _instanceSetLayout;
    VkDescriptorSet _defaultInstanceSet;
    VulkanBuffer _instanceTransforms;
    VulkanBuffer _clipSpaceBuffer;

    static AppContext instance;

};