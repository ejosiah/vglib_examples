#pragma once

#include "Solver.hpp"

class VerletSolver final : public Solver {
public:
    VerletSolver(VulkanDevice& device,
                 VulkanDescriptorPool& descriptorPool,
                 VulkanDescriptorSetLayout accStructDescriptorSetLayout,
                 VkDescriptorSet accStructDescriptorSet,
                 std::shared_ptr<Cloth> cloth,
                 std::shared_ptr<Geometry> geometry,
                 int fps = 480);

    ~VerletSolver() final = default;

protected:
    void init0() final;

    void solve0(VkCommandBuffer commandBuffer) final;

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
