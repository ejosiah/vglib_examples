#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "BindPoints.hpp"
#include "FFTOcean2.hpp"
#include "Profiler.hpp"
#include "PhysicsBody.hpp"

class FFTOceanDemo : public VulkanBaseApp{
public:
    explicit FFTOceanDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initProfiler();

    void initFFTOcean();

    void initObject();

    void initRenderGraphInputs();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createComputePipeline();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderObjects(VkCommandBuffer commandBuffer);

    void runRenderGraph(VkCommandBuffer commandBuffer);

    void renderSkyView(VkCommandBuffer commandBuffer);

    void renderArealPerspective(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void toneMap(VkCommandBuffer commandBuffer);

    void renderToDisplay(VkCommandBuffer commandBuffer);

    void computeBuoyancy(VkCommandBuffer commandBuffer);

    void updateObjects(VkCommandBuffer commandBuffer);

    void samplePoints(VkCommandBuffer commandBuffer);

    void computeSurfaceArea(VkCommandBuffer commandBuffer);

    void generateImpulses(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void newFrame() override;

    void endFrame() override;

    void cleanup() override;

    void onPause() override;

    std::vector<PipelineMetaData> metadata();

protected:
    struct {
        struct { ;
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } object;
        struct { ;
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } skyView;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } arealPerspective;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            struct {
                int method{4};
                float exposureValue{0};
            } constants;
        } toneMapper;

        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        Texture color;
        Texture depth;
        Texture extras;
    } renderGraphInputs;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    Offscreen::RenderInfo renderInfo;
    BindPoints bindPoints{};

    VulkanDescriptorSetLayout displayDescriptorSetLayout;
    VkDescriptorSet displayDescriptorSet{};

    VulkanDescriptorSetLayout subpassInputDescriptorSetLayout;
    VkDescriptorSet subpassInputDescriptorSet{};

    VulkanDescriptorSetLayout objectDescriptorSetLayout;
    VkDescriptorSet objectDescriptorSet;

    VulkanDescriptorSetLayout physicsDescriptorSetLayout;
    VkDescriptorSet physicsDescriptorSet;

    Profiler profiler;
    std::unique_ptr<FFTOcean2> ocean;

    struct Info {
        glm::mat4 transform{1};
        uint numTris{0};
        float density{0};
    };
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
        VulkanBuffer area;
        VulkanBuffer points;
        VulkanBuffer metadata;
        PhysicsBody body;
        Info* info;
    } object;

    static constexpr uint SAMPLE_COUNT = 0;
    static constexpr uint IMPULSE_COUNT = 1;
    struct {
        VulkanBuffer samplePoints;
        VulkanBuffer impulsePointsBuffer;
        VulkanBuffer sampleArea;
        VulkanBuffer counts;
        VulkanBuffer staging;
        std::span<glm::vec3> impulsePoints{};
        std::span<glm::vec3> impulses{};
        std::span<uint> sizes{};
    } physics;

    struct {
        glm::vec4 horizontalLength;
        uint heightMapIndex;
    } buoyancyConstants;

    ComputePipelines compute;

    void localReadBarrier(VkCommandBuffer commandBuffer);
};