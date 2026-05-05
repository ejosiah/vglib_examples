#pragma once

#include "VulkanBaseApp.h"

#include "AppContext.hpp"

class NeonReactorDemo : public VulkanBaseApp {
public:
    explicit NeonReactorDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createUniforms();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void syncUniforms();

    void renderUI(VkCommandBuffer commandBuffer);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer* buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) override;

    void update(float time) override;

    void cleanup() override;

    void onPause() override;

private:
    struct alignas(16) UniformData {
        glm::vec4 resolutionTime{1280.0f, 720.0f, 0.0f, 0.0f};
        glm::vec4 colorA{0.09f, 0.85f, 1.55f, 1.15f};
        glm::vec4 colorB{1.65f, 0.24f, 0.72f, 1.0f};
        glm::vec4 controlsA{1.10f, 1.85f, 2.80f, 1.05f};
        glm::vec4 controlsB{0.32f, 0.18f, 0.65f, 0.82f};
    };

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        bool animate{true};
        float speed{0.85f};
        float swirl{1.10f};
        float density{1.85f};
        float glow{2.80f};
        float bloom{1.05f};
        float pulse{0.32f};
        float grain{0.18f};
        float vignette{0.65f};
        float contrast{0.82f};
        float energy{1.15f};
        glm::vec3 primary{0.09f, 0.85f, 1.55f};
        glm::vec3 accent{1.65f, 0.24f, 0.72f};
    } options;

    VulkanDescriptorPool descriptorPool;
    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    VulkanBuffer uniformBuffer;
    UniformData* uniforms{};
    VkDescriptorSet descriptorSet{};
    float animationTime{0.0f};
};
