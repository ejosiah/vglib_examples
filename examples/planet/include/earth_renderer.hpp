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
        VulkanDescriptorSetLayout textureDescriptorSetLayout;
        VkDescriptorSet milkywayDescriptorSet;
    };
    EarthRenderer() = default;

    EarthRenderer(const Params& params);

    void initialize();

    void render(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet, bool isVisible);

    void createPipeline();

protected:
    void render_mesh(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet);

    void render_impostor(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet);

    void createLayoutDescriptorSet();

    void updateDescriptorSetLayout();

private:
    VulkanDevice* m_device;
    Planet* m_planet;
    WaterData* m_waterData;

    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    VulkanDescriptorSetLayout m_textureDescriptorSetLayout;
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};
    VkDescriptorSet m_milkywayDescriptorSet{};

    VulkanPipelineLayout m_layout;
    VulkanPipeline m_pipeline;
    VulkanPipelineLayout m_impostorLayout;
    VulkanPipeline m_impostorPipeline;
};
