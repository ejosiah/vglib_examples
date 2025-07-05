#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayQuerySupport.hpp"
#include "ComputePipelins.hpp"
#include "Offscreen.hpp"

#include "vulkan_cuda_interop.hpp"
#include "vulkan_denoiser.hpp"

namespace CommandBufferGroups  {
    static constexpr int Render = 0;
    static constexpr int PreDenoise = 1;
    static constexpr int PostDenoise = 2;
    static constexpr int Count = 3;
};

namespace CBG = CommandBufferGroups;

class RtxAmbientOcclusion : public VulkanBaseApp, public VulkanRayQuerySupport {
public:
    explicit RtxAmbientOcclusion(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initDenoiser();

    void initOffscreen();

    void initAsBuilder();

    void initGBuffer();

    void createNoiseTextures();

    VulkanSampler createNoiseSampler();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void loadModel();

    void writeToGBuffer(VkCommandBuffer commandBuffer);

    void denoise();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderScene(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void computeAO(VkCommandBuffer commandBuffer);

    std::vector<PipelineMetaData> pipelineMetaData();

    void endFrame() override;

protected:
    struct {
        Pipeline quad;
        Pipeline gBuffer;
    } render;

    struct {
        Texture color;
        Texture position;
        Texture normal;
        Texture ambientOcclusion;
        Texture depth;
        VkDescriptorSet descriptorSet{};
    } gBuffer;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    BindlessDescriptor bindlessDescriptor;
    VulkanDrawable model;
    VulkanDrawable plane;
    rt::AccelerationStructureBuilder accStructBuilder;
    VulkanDescriptorSetLayout gBufferDescriptorSetLayout;
    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VulkanDescriptorSetLayout imageDescriptorSetLayout;
    Offscreen offscreen;
    Offscreen::RenderInfo renderInfo;
    Texture aoResult;
    VulkanDescriptorSetLayout accStructDescriptorSetLayout;
    VkDescriptorSet accStructDescriptorSet{};
    VkDescriptorSet whiteNoiseDescriptorSet{};
    VkDescriptorSet blueNoiseDescriptorSet{};
    VkDescriptorSet ambientDescriptorSet{};
    std::unique_ptr<ComputePipelines> compute;

    static constexpr int NoiseCount = 64;

    struct {
        int sampleCount = 8;
        float radius = 2;
        int frame{};
    } constants;

    struct {
        Texture texture;
        VkDescriptorSet descriptorSet{};
        bool blueNoise{true};
    } noise;

    bool shouldDenoise{};

    cuda::Semaphore denoiseSemaphore;
    std::unique_ptr<VulkanDenoiser> denoiser;
    std::shared_ptr<OptixContext> optix;
    uint64_t fenceValue{0};
    VkTimelineSemaphoreSubmitInfo denoiseTimelineInfo{
            VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO
    };

};