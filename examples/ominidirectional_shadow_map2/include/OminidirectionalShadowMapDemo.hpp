#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "PointShadowMap.hpp"

class OminidirectionalShadowMapDemo : public VulkanBaseApp{
public:
    explicit OminidirectionalShadowMapDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void loadModel();

    void createLights();

    void renderLight(VkCommandBuffer commandBuffer);

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void initShadowMap();

    void endFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        glm::mat4 model{1};
    } light;

    glm::vec3 lightPosition{0.0f, 1.5, 0.0};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    std::shared_ptr<gltf::Model> model;
    BindlessDescriptor bindlessDescriptor;
    PointShadowMap shadowMap;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } lightObj;
};