#include "VulkanBaseApp.h"
#include "Canvas.hpp"

class SubGroupEval : public VulkanBaseApp{
public:
    explicit SubGroupEval(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initTexture();

    void initCanvas();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

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
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;

    VulkanDescriptorSetLayout imageDescriptorSetLayout;
    VkDescriptorSet imageDescriptorSet;
    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VkDescriptorSet textureDescriptorSet;
    Texture texture;
    Canvas canvas;

};