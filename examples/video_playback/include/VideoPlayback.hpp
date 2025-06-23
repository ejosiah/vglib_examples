#pragma once
#include "VulkanBaseApp.h"
#include "video/Video.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"
#include "video/VideoDecoder.hpp"

class VideoPlayback : public VulkanBaseApp{
public:
    explicit VideoPlayback(const Settings& settings = {});

protected:
    void initApp() override;

    void initVideoDecoder();

    void createSampler();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorBinding(const Texture& texture);

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void beforeDeviceCreation() override;

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void loadVideo();

    void initVideoInstance();

    void renderControls(VkCommandBuffer commandBuffer);

    void endFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::shared_ptr<Video> video;
    std::shared_ptr<VideoInstance> video_instance;
    VulkanDescriptorSetLayout displayDescriptorSetLayout;
    VkDescriptorSet displayDescriptorSet{};
    std::unique_ptr<VideoDecoder> decoder;
    VulkanSampler sampler;
};