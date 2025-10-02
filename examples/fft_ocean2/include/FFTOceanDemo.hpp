#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "BindPoints.hpp"
#include "FFTOcean2.hpp"
#include "Profiler.hpp"

class FFTOceanDemo : public VulkanBaseApp{
public:
    explicit FFTOceanDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initProfiler();

    void initFFTOcean();

    void initRenderGraphInputs();

    void initBindlessDescriptor();

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

    void runRenderGraph(VkCommandBuffer commandBuffer);

    void renderSkyView(VkCommandBuffer commandBuffer);

    void renderArealPerspective(VkCommandBuffer commandBuffer);

    void toneMap(VkCommandBuffer commandBuffer);

    void renderToDisplay(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void newFrame() override;

    void endFrame() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
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

    Profiler profiler;
    std::unique_ptr<FFTOcean2> ocean;

    void localReadBarrier(VkCommandBuffer commandBuffer);
};