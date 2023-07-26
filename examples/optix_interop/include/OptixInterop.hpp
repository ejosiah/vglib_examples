#pragma once
#include "VulkanBaseApp.h"
#include "optix_wrapper.hpp"

class OptixInterop : public VulkanBaseApp{
public:
    explicit OptixInterop(const Settings& settings = {});

protected:
    void initApp() override;

    void initCheckerBoard();

    void createCheckerboard();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;


    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    Texture checkerboard;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;
    VulkanBuffer interopBuffer;
    OptixWrapper optix;
};