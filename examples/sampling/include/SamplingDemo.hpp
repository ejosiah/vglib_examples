#include "VulkanBaseApp.h"
#include "sampling.hpp"

class SamplingDemo : public VulkanBaseApp{
public:
    explicit SamplingDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadEnvMap();

    void createScreenBuffer();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

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
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanBuffer screenBuffer;

    VulkanDescriptorSetLayout distSetLayout;
    VkDescriptorSet distDescriptorSet;

    Texture envMap;
    Distribution2DTexture envMapDistribution;
};