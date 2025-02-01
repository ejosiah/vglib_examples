#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "CascadeShadowMap.hpp"

struct UniformData {
    glm::vec3 lightDir;
    int numCascades;
    int usePCF;
    int colorCascades;
    int showExtents;
    int shadowOn;
};

class CascadeShadowMapDemo : public VulkanBaseApp{
public:
    explicit CascadeShadowMapDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initBindlessDescriptor();

    void loadModel();

    void initUniforms();

    void initShadowMaps();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderScene(VkCommandBuffer commandBuffer, VulkanPipeline& pipeline, VulkanPipelineLayout& layout);

    void renderUI(VkCommandBuffer commandBuffer);

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
    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    std::shared_ptr<gltf::Model> model;
    BindlessDescriptor bindlessDescriptor;
    VulkanDescriptorSetLayout lightDescriptorSetLayout;
    VkDescriptorSet lightDescriptorSet;
    CascadeShadowMap shadowMap;
    glm::vec3 lightDirection{1};
    std::span<float> splitDepth;
    VulkanBuffer splitDepthBuffer;
    float splitLambda{0.95};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu;
    } ubo;
    bool showShadowMap{};
};