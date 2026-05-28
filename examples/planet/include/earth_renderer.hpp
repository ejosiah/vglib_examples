#pragma once

#include "camera_base.h"
#include "planet.hpp"

class EarthRenderer {
public:
    struct Params {
        VulkanDevice &device;
        Planet &planet;
        VulkanDescriptorSetLayout globalDescriptorSetLayout;
    };
    EarthRenderer() = default;

    EarthRenderer(const Params& params);

    void initialize();

    void render(VkCommandBuffer commandBuffer, const BaseCameraController& camera, VkDescriptorSet& globalDescriptorSet);

    void createPipeline();

protected:
    void createLayoutDescriptorSet();

    void updateDescriptorSetLayout();

private:
    VulkanDevice* m_device;
    Planet* m_planet;

    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    VulkanPipelineLayout m_layout;
    VulkanPipeline m_pipeline;
};