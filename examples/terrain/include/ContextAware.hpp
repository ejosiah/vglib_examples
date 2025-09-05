#pragma once

#include "Shared.hpp"

struct Context {
    VulkanDevice* device{};
    VulkanDescriptorPool* descriptorPool{};
    GBuffer* gBuffer{};
    BaseCameraController* camera{};
    BindlessDescriptor* bindlessDescriptor{};
    std::unique_ptr<Prototypes> prototypes;
    Frustum viewProjectionFrustum;
    glm::mat4 view{1};
    glm::mat4 viewProjection{1};
    glm::mat4 inverseViewProjection{1};
    glm::mat4 inverseView{1};
    glm::mat4 inverseProjection{1};
    glm::ivec4 mouse;
    glm::vec3 lightDirection{};
    float lightIntensity{1};
    bool useBruneton{};
    float exposure{10};
    uint screenWidth;
    uint screenHeight;
    uint dmap_tex_index{~0u};
    uint dmap_normal_tex_index{~0u};
    uint dmap_shadow_tex_index{~0u};
    uint transmittanceTextureIndex{~0u};
    uint multiScatteringTextureIndex{~0u};
    uint skyViewTextureIndex{~0u};
    uint arealPerspectiveTextureIndex{~0u};
    uint gBufferColorIndex{~0u};
    uint gBufferPositionIndex{~0u};
    uint gBufferNormalIndex{~0u};
    uint gBufferDepthIndex{~0u};
};

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