#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"

struct RenderPipeline {
    VulkanPipelineLayout layout;
    VulkanPipeline pipeline;
};

class VolumetricIntegration : public VulkanBaseApp{
public:
    explicit VolumetricIntegration(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createLights();

    void createGBuffer();

    void initBindlessDescriptor();

    void loadModel();

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

    void renderScene(VkCommandBuffer commandBuffer, const RenderPipeline& pipeline);

    void evaluateLighting(VkCommandBuffer commandBuffer);

    void updateGBuffer();

    void endFrame() override;

    void renderLight(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        RenderPipeline gbuffer;
        RenderPipeline forward;
        RenderPipeline eval_lighting;
        RenderPipeline light;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::shared_ptr<gltf::Model> model;
    VulkanBuffer lightBuffer;
    std::span<gltf::Light> lights;
    VulkanDescriptorSetLayout sceneLightDescriptorSetLayout;
    VkDescriptorSet sceneLightsDescriptorSet{};
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } lightObj;

    struct {
        Texture position;
        Texture normal;
        Texture color;
        Texture metalRoughnessAmb;
        Texture depth;
        Offscreen::RenderInfo renderInfo{};
    } gbuffer;
    Offscreen offscreen{};

    static constexpr uint32_t RESERVED_TEXTURE_SLOTS = 5;

};