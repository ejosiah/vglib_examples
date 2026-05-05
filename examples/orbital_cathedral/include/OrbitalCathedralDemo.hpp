#pragma once

#include "VulkanBaseApp.h"
#include "AppContext.hpp"

class OrbitalCathedralDemo : public VulkanBaseApp {
public:
    explicit OrbitalCathedralDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createCamera();

    void createBuffers();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void updateInstances();

    void syncUniforms();

    void renderCathedral(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer* buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

private:
    static constexpr uint32_t kLayers = 120;
    static constexpr uint32_t kSpokes = 28;
    static constexpr uint32_t kSpineCount = 80;
    static constexpr uint32_t kInstanceCount = kLayers * kSpokes + kSpineCount;

    struct alignas(16) MaterialData {
        glm::vec4 colorA{0.14f, 0.70f, 1.20f, 1.55f};
        glm::vec4 colorB{1.35f, 0.42f, 0.18f, 0.95f};
        glm::vec4 lightDirTime{0.45f, 0.80f, -0.35f, 0.0f};
        glm::vec4 controls{1.35f, 3.20f, 4.80f, 0.72f};
    };

    struct {
        bool animate{true};
        float speed{0.75f};
        float twist{2.40f};
        float radius{4.60f};
        float span{15.0f};
        float pulse{0.75f};
        float lift{1.10f};
        float glow{1.35f};
        float banding{3.20f};
        float shimmer{4.80f};
        float contrast{0.72f};
        glm::vec3 primary{0.14f, 0.70f, 1.20f};
        glm::vec3 accent{1.35f, 0.42f, 0.18f};
    } options;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanDescriptorSetLayout materialSetLayout;
    VkDescriptorSet instanceSet{};
    VkDescriptorSet materialSet{};
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanBuffer instanceBuffer;
    VulkanBuffer materialBuffer;
    MaterialData* material{};
    std::vector<glm::mat4> instanceMatrices;
    float animationTime{0.0f};
};
