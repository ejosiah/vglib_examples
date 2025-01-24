#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "PointShadowMap.hpp"

struct RenderPipeline {
    VulkanPipelineLayout layout;
    VulkanPipeline pipeline;
};

enum class RenderMode :int { Forward, Deferred };

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

    void loadTextures();

    void initShadowMap();

    void updateTextureBinding(std::shared_ptr<gltf::TextureUploadStatus> status);

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

    void rayMarch(VkCommandBuffer commandBuffer);

    void updateGBuffer();

    void endFrame() override;

    void renderLight(VkCommandBuffer commandBuffer);

    void renderSceneToShadowMap(VkCommandBuffer commandBuffer);

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
        RenderPipeline ray_march;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
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
    Texture grayNoise;

    struct {
        glm::mat4 model{};
        glm::mat4 view{};
        glm::mat4 projection{};
        glm::vec3 camera_position{};
        float time{};
        float fog_strength{0};
        float stepScale{0.25};
        int fog_noise{1};
        int enable_volume_shadow{1};
        int use_improved_integration{1};
        int update_transmission_first{0};
        int constantFog{1};
        int heightFog{};
        int boxFog{};
    } fogConstants{};

    RenderMode renderMode{RenderMode::Forward};
    PointShadowMap shadowMap;

    static constexpr uint32_t RESERVED_TEXTURE_SLOTS = 8;

};