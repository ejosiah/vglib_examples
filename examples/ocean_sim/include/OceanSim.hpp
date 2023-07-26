#include "VulkanBaseApp.h"

class OceanSim : public VulkanBaseApp{
public:
    explicit OceanSim(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadEnvironmentMap();

    void initBuffers();

    void loadQuadPatch();

    void initTerrain();

    void initOcean();

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

    void renderSkyBox(VkCommandBuffer commandBuffer);

    void renderTerrain(VkCommandBuffer commandBuffer);

    void renderOcean(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } sky;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;

    VulkanDescriptorSetLayout environmentSetLayout;
    VkDescriptorSet environmentSet;

    VulkanDescriptorSetLayout heightMapSetLayout;
    VkDescriptorSet heightMapSet;

    struct {
        Texture brdfLUT;
        Texture environmentMap;
        Texture diffuseEnvironmentMap;
    } environmentTextures;

    struct {
        glm::mat4 transform;
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        float width{52.66 * km};
        float height{52.66 * km};
        float zMax{1.587 * km};
        float zMin{-0.014 * km};
    } terrain;

    struct {
        VulkanBuffer vertexBuffer;
        uint32_t numPatches;
        uint32_t numVertices;
    } patch;

    struct {
        glm::mat4 transform;
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        float width{128 * km};
        float height{128 * km};
    } ocean;

    Texture heightMap;

    struct {
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
    } skyBox;
};