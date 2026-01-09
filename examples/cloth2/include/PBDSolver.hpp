#pragma once

#include "Solver.hpp"

namespace SolverType {
    constexpr int GRAPH_COLOR = 0;
    constexpr int JACOBI = 1;
    constexpr int COMBINED = 2;
};

class PBDSolver final : public Solver {
public:
    friend class ClothDemo2;

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

    void solve0(VkCommandBuffer commandBuffer) final;

    void integrate(VkCommandBuffer commandBuffer);

    void solveConstraints(VkCommandBuffer commandBuffer);

    void solveConstraints(VkCommandBuffer commandBuffer, int solver, int cOffset);

    void jacobiSolve(VkCommandBuffer commandBuffer, int cOffset = 0);

    void addCorrections(VkCommandBuffer commandBuffer);

    void updateVelocity(VkCommandBuffer commandBuffer);

    static void clear(VkCommandBuffer commandBuffer, const VulkanBuffer& buffer);

    void postInit() override;

    std::vector<PipelineMetaData> pipelineMetaData0() override;

private:
    void createDescriptorSetLayout();

    void createBuffers();

    void initPositions();

    void generateConstraints();

    void createVelocityBuffer();

    void createCorrectionsBuffer();

    void createMassBuffer();

    void updateDescriptorSets();

    void computeRestLengths();

    static std::vector<int> compileConstraintIds(size_t numConstraints, size_t numX, size_t numY);

private:
    std::array<VulkanBuffer, 2> positions;
    VulkanBuffer restPositions;
    VulkanBuffer velocity;
    VulkanBuffer corrections;
    VulkanBuffer constraints;
    VulkanBuffer constraintIDs;
    VulkanBuffer restLengths;
    VulkanBuffer velocities;
    VulkanBuffer invMass;
    VulkanDescriptorSetLayout positionSetLayout;
    VulkanDescriptorSetLayout descriptorSetLayout;
    std::array<VkDescriptorSet, 2> positionDescriptorSet{};
    VkDescriptorSet descriptorSet;
    std::vector<VkDescriptorSet> sets;
    uint32_t numConstraints{0};
    std::array<int, 5> passSizes{};
    std::array<bool, 5> independentPass{true, true, true, false};
    bool combineSolvers{true};
};