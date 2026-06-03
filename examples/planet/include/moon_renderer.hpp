#pragma once

#include "MoonMaterial.hpp"
#include "planet.hpp"

class MoonRenderer {
public:
    struct Params {
        VulkanDevice& device;
        Planet& planet;
        MoonMaterial& material;
        VulkanDescriptorSetLayout globalDescriptorSetLayout;
    };

    MoonRenderer() = default;

    MoonRenderer(const Params& params);

    void initialize();

    void render(VkCommandBuffer commandBuffer, VkDescriptorSet& globalDescriptorSet);

    void createPipeline();

protected:
    void createLayoutDescriptorSet();

    void updateDescriptorSetLayout();

private:
    VulkanDevice* m_device{};
    Planet* m_planet{};
    MoonMaterial* m_material{};

    VulkanDescriptorSetLayout m_globalDescriptorSetLayout;
    VulkanDescriptorSetLayout m_descriptorSetLayout;
    VkDescriptorSet m_descriptorSet{};

    VulkanPipelineLayout m_layout;
    VulkanPipeline m_pipeline;
};
