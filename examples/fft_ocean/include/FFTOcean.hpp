#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "FFT.hpp"
#include "imgui.h"
#include "Canvas.hpp"

class FFTOcean : public VulkanBaseApp{
public:
    explicit FFTOcean(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initCanvas();

    void initFFT();

    void initScreenQuad();

    void createPatch();

    std::vector<glm::vec4> createDistribution(float mu = 0, float sigma = 1);

    void initTextures();

    void loadInputSignal();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void createComputePipeline();

    void createHeightField(VkCommandBuffer commandBuffer);

    void createDistribution(VkCommandBuffer commandBuffer);

    void createSpectralComponents(VkCommandBuffer commandBuffer);

    void createSpectralHeightField(VkCommandBuffer commandBuffer);

    void extractHeightFieldMagnitude(VkCommandBuffer commandBuffer);

    void copyToCanvas(VkCommandBuffer commandBuffer, const VulkanImage& source);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderOcean(VkCommandBuffer commandBuffer);

    void renderScreenQuad(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } ocean;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            int action{4};
        } debug;
    } render;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } distribution;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } spectral_components;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } spectral_height_field;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } height_field_mag;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Canvas canvas;

    Texture distribution;
    Texture tilde_h0k;
    Texture tilde_h0_minus_k;
    VulkanDescriptorSetLayout imageDescriptorSetLayout;
    VulkanDescriptorSetLayout complexSetLayout;

    struct {
        VkDescriptorSet distributionImageSet{};
        VkDescriptorSet tilde_h0k{};
        VkDescriptorSet tilde_h0_minus_k{};
    } descriptorSets;

    struct {
        ComplexSignal signal;
        VkDescriptorSet descriptorSet;
    } hkt;

    struct {
        glm::vec2 windDirection{1, 1};
        float windSpeed{100};
        float amplitude{4};
        const float horizontalLength{1000};
        float time{0};
        float windPower{2};
    } constants;

    struct {
        Camera camera;
        float time{0};
    } renderConstants;

    static constexpr uint32_t hSize = 1024;

    Texture terrainTextures;
    ComplexSignal inputSignal;

    struct {
       ComplexSignal signal;
       VkDescriptorSet descriptorSet{};
    } heightFieldSpectral;

    struct {
        Texture texture;
        VkDescriptorSet descriptorSet{};
    } heightField;
    VulkanBuffer patch;
    uint32_t numPatches{25};
    FFT fft;
    VulkanBuffer screenQuad;
    Action* debugAction{};
};