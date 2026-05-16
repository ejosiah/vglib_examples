#pragma once

#include "Shared.hpp"
#include "Profiler.hpp"
#include "InputManager.h"

struct Context {
    VulkanDevice* device{};
    VulkanDescriptorPool* descriptorPool{};
    RenderGraphInputs* rgInputs{};
    BaseCameraController* camera{};
    BindlessDescriptor* bindlessDescriptor{};
    Mouse* mouseInput{};
    std::unique_ptr<Prototypes> prototypes;
    Frustum viewProjectionFrustum;
    VulkanSampler edgeClampSampler;
    Profiler* profiler{};
    glm::mat4 view{1};
    glm::mat4 viewProjection{1};
    glm::mat4 inverseViewProjection{1};
    glm::mat4 inverseView{1};
    glm::mat4 inverseProjection{1};
    glm::ivec4 mouse{0};
    glm::vec3 lightDirection{};
    float lightIntensity{1};
    float elapsedTime{0};
    uint screenWidth;
    uint screenHeight;
    uint dmap_tex_index{~0u};
    uint dmap_normal_tex_index{~0u};
    uint dmap_slope_moments0_tex_index{~0u};
    uint dmap_slope_moments1_tex_index{~0u};
    uint dmap_shadow_tex_index{~0u};
    uint transmittanceTextureIndex{~0u};
    uint multiScatteringTextureIndex{~0u};
    uint skyViewTextureIndex{~0u};
    uint arealPerspectiveTextureIndex{~0u};
    uint radianceTextureIndex{~0u};
    uint positionTextureIndex{~0u};
    uint depthTextureIndex{~0u};
    VulkanDescriptorSetLayout subpassInputDescriptorSetLayout;
    VkDescriptorSet subpassInputDescriptorSet{};
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

    RenderGraphInputs& renderGraphInputs() {
        return *context().rgInputs;
    }

    auto resource(auto path) const {
        return FileManager::resource(path);
    }

    Mouse& mouseInput()  {
        return *context().mouseInput;
    }

    auto& profiler()  {
        return *context().profiler;
    }
};
