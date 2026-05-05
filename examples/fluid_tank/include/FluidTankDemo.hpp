#pragma once

#include "VulkanBaseApp.h"
#include "AppContext.hpp"
#include "ComputePipelins.hpp"

class FluidTankDemo : public VulkanBaseApp {
public:
    explicit FluidTankDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void initCamera();

    void createTextures();

    void initDensityField();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipelines();

    void updateSimulationUniforms();

    void updateRenderUniforms();

    void updateSphere(float dt);

    void simulate();

    void renderUI(VkCommandBuffer commandBuffer);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer* buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

private:
    static constexpr uint32_t GridX = 48;
    static constexpr uint32_t GridY = 64;
    static constexpr uint32_t GridZ = 48;

    struct Field3D {
        std::array<Texture, 2> tex{};
    };

    struct alignas(16) SimulationUniform {
        glm::ivec4 gridSize{GridX, GridY, GridZ, 0};
        glm::vec4 invGridSizeDt{1.0f / GridX, 1.0f / GridY, 1.0f / GridZ, 1.0f / 60.0f};
        glm::vec4 spherePosRadius{0.5f, 1.08f, 0.5f, 0.08f};
        glm::vec4 sphereVelocity{0.0f};
        glm::vec4 fluidParams{0.48f, -0.55f, 3.5f, 0.998f};
    };

    struct alignas(16) RenderUniform {
        glm::vec4 tankMin{-0.6f, -0.05f, -0.38f, 0.0f};
        glm::vec4 tankMax{0.6f, 1.1f, 0.38f, 0.0f};
        glm::vec4 waterColor{0.12f, 0.44f, 0.76f, 0.0f};
        glm::vec4 renderParams{0.12f, 3.6f, 0.02f, 0.68f};
    };

    struct {
        bool running{true};
        float timeScale{1.0f};
        int pressureIterations{20};
        float fillLevel{0.48f};
        float gravity{-0.55f};
        float splash{3.5f};
        float dissipation{0.998f};
        float threshold{0.12f};
        float absorption{3.6f};
        float stepScale{0.02f};
        float glass{0.68f};
        glm::vec3 waterColor{0.12f, 0.44f, 0.76f};
    } options;

    struct {
        glm::vec3 position{0.5f, 1.08f, 0.5f};
        glm::vec3 velocity{0.0f};
        float radius{0.08f};
        bool resetRequested{};
    } sphere;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    ComputePipelines compute;

    VulkanDescriptorSetLayout simulationSetLayout;
    VulkanDescriptorSetLayout computeSetLayout;
    VulkanDescriptorSetLayout renderSetLayout;

    VulkanBuffer simulationUniformBuffer;
    VulkanBuffer renderUniformBuffer;
    SimulationUniform* simulationUniform{};
    RenderUniform* renderUniform{};
    VkDescriptorSet simulationUniformSet{};

    Field3D velocity;
    Field3D density;
    Field3D pressure;
    Texture divergence;
    Texture obstacle;
    Texture densityForward;
    Texture densityBackward;
    Texture dummyScalar;
    Texture dummyVector;

    std::array<VkDescriptorSet, 2> advectVelocitySet{};
    std::array<VkDescriptorSet, 2> applyForcesSet{};
    VkDescriptorSet divergenceSet{};
    std::array<VkDescriptorSet, 2> jacobiSet{};
    std::array<VkDescriptorSet, 2> projectSet{};
    std::array<VkDescriptorSet, 2> densityForwardSet{};
    std::array<VkDescriptorSet, 2> densityReverseSet{};
    std::array<VkDescriptorSet, 2> densityCorrectSet{};
    VkDescriptorSet obstacleSet{};
    VkDescriptorSet renderSet{};
};
