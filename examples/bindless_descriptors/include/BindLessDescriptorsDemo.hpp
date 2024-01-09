#include "VulkanBaseApp.h"
#include "AsyncModelLoader.hpp"
#include "plugins/BindLessDescriptorPlugin.hpp"

class BindLessDescriptorsDemo : public VulkanBaseApp{
public:
    explicit BindLessDescriptorsDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initGlobals();

    void loadModel();

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
    std::unique_ptr<FirstPersonCameraController> camera;
    VulkanDrawable sponza;
    std::shared_ptr<asyncml::Model> _sponza;
    std::unique_ptr<asyncml::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    VulkanDescriptorSetLayout meshSetLayout;
    VkDescriptorSet meshDescriptorSet;
    Texture dummyTexture;
};