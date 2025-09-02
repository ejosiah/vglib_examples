#pragma once

#include "Shared.hpp"

class ContextAware {
protected:
    virtual Context& context() = 0;

    VulkanDevice& device() {
        return *context().device;
    }

    BaseCameraController& camera() {
        return *context().camera;
    }

    VulkanDescriptorPool& descriptorPool() {
        return *context().descriptorPool;
    }

    GraphicsPipelineBuilder graphicsPipelineBuilder() {
        return context().prototypes->cloneGraphicsPipeline();
    }

    GraphicsPipelineBuilder clipSpacePipelineBuilder() {
        return context().prototypes->cloneScreenSpaceGraphicsPipeline();
    }

    VulkanDescriptorSetLayout& bindlessDescriptorSetLayout() {
        return *const_cast<VulkanDescriptorSetLayout*>(context().bindlessDescriptor->descriptorSetLayout);
    }

    VkDescriptorSet bindlessDescriptorSet() {
        return context().bindlessDescriptor->descriptorSet;
    }

    BindlessDescriptor& bindlessDescriptor() {
        return *context().bindlessDescriptor;
    }

    auto resource(auto path) const {
        return FileManager::resource(path);
    }
};