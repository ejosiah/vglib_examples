#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "CascadeShadowMap.hpp"

struct UniformData {
    glm::vec3 lightDir;
    int numCascades;
    int usePCF;
    int colorCascades;
    int showExtents;
    int colorShadow;
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

    void createCube();

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

    void renderFrustum(VkCommandBuffer commandBuffer);

    void renderCamera(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void computeCameraBounds();

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            struct {
                Camera camera{};
                int cascadeIndex{0};
            } constants;
        } frustum;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } model;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } camera;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> sceneCamera;
    std::unique_ptr<FirstPersonCameraController> debugCamera;
    FirstPersonCameraController* camera{};
    std::unique_ptr<gltf::Loader> loader;
    std::shared_ptr<gltf::Model> model;
    BindlessDescriptor bindlessDescriptor;
    VulkanDescriptorSetLayout lightDescriptorSetLayout;
    VkDescriptorSet lightDescriptorSet;
    CascadeShadowMap shadowMap;
    glm::vec3 lightDirection{1};
    std::span<float> splitDepth;
    VulkanBuffer splitDepthBuffer;
    float splitLambda{CascadeShadowMap::DEFAULT_CASCADE_SLIT_LAMBDA};

    struct {
        VulkanBuffer gpu;
        UniformData* cpu;
    } ubo;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } cube;
    bool showShadowMap{};
    bool freezeShadowMap{};
    bool showFrustum{};

    struct {
        VulkanBuffer vertices;
        glm::mat4 transform{1};
        glm::mat4 aabb{1};
    } cameraBounds;
    bool showCamera{};
    bool freezePressed = false;
};