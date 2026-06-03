#pragma once

#include "planet.hpp"

class WaterData;

class EarthRenderer {
public:
    struct Params {
        VulkanDevice &device;
        Planet &planet;
        WaterData& waterData;
        VulkanDescriptorSetLayout globalDescriptorSetLayout;
    };
    EarthRenderer() = default;

    EarthRenderer(const Params& params);

    void initialize();

    void render(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet);

    void createPipeline();

protected:
    void createLayoutDescriptorSet();

    void updateDescriptorSetLayout();

private:
    VulkanDevice* m_device;
    Planet* m_planet;
    WaterData* m_waterData;

    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    VulkanPipelineLayout m_layout;
    VulkanPipeline m_pipeline;
};
