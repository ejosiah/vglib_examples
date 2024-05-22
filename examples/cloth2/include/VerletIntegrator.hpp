#pragma once

#include "Integrator.hpp"

class VerletIntegrator final : public Integrator {
public:
    VerletIntegrator(VulkanDevice& device,
                     VulkanDescriptorPool& descriptorPool,
                     VulkanDescriptorSetLayout accStructDescriptorSetLayout,
                     VkDescriptorSet accStructDescriptorSet,
                     std::shared_ptr<Cloth> cloth,
                     std::shared_ptr<Geometry> geometry,
                     int fps = 480);

    ~VerletIntegrator() final = default;

protected:
    void init0() final;

    void integrate0(VkCommandBuffer commandBuffer) final;

    std::vector<PipelineMetaData> pipelineMetaData0() override;

private:
    void createDescriptorSetLayout();

    void createBuffers();

    void updateDescriptorSets();

private:
    std::array<VulkanBuffer, 2> positions;
    VulkanBuffer normals;
    VulkanDescriptorSetLayout descriptorSetLayout;
    std::array<VkDescriptorSet, 2> descriptorSet{};
    std::vector<VkDescriptorSet> sets;
};
