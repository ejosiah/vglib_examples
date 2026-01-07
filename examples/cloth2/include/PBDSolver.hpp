#pragma once

#include "Solver.hpp"

class PBDSolver final : public Solver {
public:
    PBDSolver(VulkanDevice& device,
              VulkanDescriptorPool& descriptorPool,
              VulkanDescriptorSetLayout accStructDescriptorSetLayout,
              VkDescriptorSet accStructDescriptorSet,
              std::shared_ptr<Cloth> cloth,
              std::shared_ptr<Geometry> geometry,
              int fps = 480);

    ~PBDSolver() final = default;

protected:
    void init0() final;

    void generateConstraints();

    void solve0(VkCommandBuffer commandBuffer) final;

    void solveConstraints(VkCommandBuffer commandBuffer);

    void addCorrections(VkCommandBuffer commandBuffer);

    void updateVelocity(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> pipelineMetaData0() override;

private:
    void createDescriptorSetLayout();

    void createBuffers();

    void updateDescriptorSets();

private:
    std::array<VulkanBuffer, 2> positions;
    VulkanBuffer restPositions;
    VulkanBuffer velocity;
    VulkanBuffer corrections;
    VulkanBuffer constraints;
    VulkanDescriptorSetLayout descriptorSetLayout;
    std::array<VkDescriptorSet, 2> descriptorSet{};
    std::vector<VkDescriptorSet> sets;
};