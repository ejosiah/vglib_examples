#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "Shared.hpp"
#include "Terrain.hpp"
#include "DisplacementMapGenerator.hpp"
#include "DisplacementShadowMap.hpp"
#include "ComputePipelins.hpp"
#include "AtmosphereModel.hpp"

class TerrainDemo : public VulkanBaseApp{
public:
    explicit TerrainDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initContext();

    void initGBuffer();

    void initUniforms();

    void initDisplacementMapGenerator();

    void initDisplacementShadowMap();

    void initTerrain();

    void initAtmosphere();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void createComputePipelines();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    void newFrame() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void runRenderGraph(VkCommandBuffer commandBuffer);

    void renderToDisplay(VkCommandBuffer commandBuffer);

    void toneMap(VkCommandBuffer commandBuffer);

    void computeLighting(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    static void localReadBarrier(VkCommandBuffer commandBuffer);

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

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            int method{3};
            float exposureValue{0};
        } constants;
    } toneMapper;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    GBuffer gBuffer;
    Offscreen::RenderInfo renderInfo;
    Offscreen::RenderInfo renderInfo1;

    RenderGraphInputs renderGraphInputs;
    Context context;
    std::unique_ptr<DisplacementMapGenerator> displacementMapGenerator;
    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<DisplacementShadowMap> displacementShadowMap;
    std::unique_ptr<AtmosphereModel> atmosphere;
    Texture heightMap;
    Texture normalMap;
    ComputePipelines compute;

    glm::vec3 lightDirection;

    struct UniformData {
        glm::mat4 inverseProjection{};
        glm::mat4 inverseView{};

        glm::vec3 sunDirection{};
        uint gBufferColorIndex{~0u};

        glm::vec3 cameraPosition{};
        uint gBufferPositionIndex{~0u};

        glm::vec3 whitePoint{1};
        uint gBufferNormalIndex{~0u};

        glm::vec2 resolution{};
        glm::vec2 sunSize{};

        float exposure{10};
        uint gBufferDepthIndex{~0u};
        uint shadowMapIndex{~0u};
    };

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VkDescriptorSet uniformDescriptorSet{};

    VulkanDescriptorSetLayout displayDescriptorSetLayout;
    VkDescriptorSet displayDescriptorSet{};

    struct  {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;

    struct {
        float lightZenith{45};
        float lightAzimuth{45};
        float exposure{10};
        bool bruneton{false};
        bool debug{true};
    } options;
};