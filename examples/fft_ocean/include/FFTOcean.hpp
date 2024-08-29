#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "FFT.hpp"
#include "imgui.h"
#include "Canvas.hpp"
#include "Offscreen.hpp"
#include "Scene.hpp"
#include "NormalMapping.hpp"

class FFTOcean : public VulkanBaseApp{
public:
    explicit FFTOcean(const Settings& settings = {});

protected:
    void initApp() override;

    void recordAudio();

    void initCamera();

    void initCanvas();

    void initFFT();

    void createWindControl();

    void initScreenQuad();

    void initAtmosphere();

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

    void generateGradientMap(VkCommandBuffer commandBuffer);

    void addBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanImage*>& images);

    void copyToCanvas(VkCommandBuffer commandBuffer, const VulkanImage& source);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderAtmosphere(VkCommandBuffer commandBuffer);

    void renderOcean(VkCommandBuffer commandBuffer);

    void renderScreenQuad(VkCommandBuffer commandBuffer);

    void renderWindControl(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void endFrame() override;

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
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } windControl;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } atmosphere;
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
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } gradient;
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
        glm::vec2 windDirection{-0.4, -0.9};
        float windSpeed{100};
        float A{20};
        const float horizontalLength{1000};
        float time{0};
        float windPower{2};
    } constants;

    static constexpr uint32_t hSize = 1024;

    Texture terrainTextures;
    ComplexSignal inputSignal;

    struct {
       ComplexSignal signal;
       VkDescriptorSet descriptorSet{};
    } heightFieldSpectral;

    struct {
        Texture texture;
        VkDescriptorSet imageDescriptorSet{};
        VkDescriptorSet textureDescriptorSet{};
    } heightField;

    struct {
        Texture texture;
        VkDescriptorSet descriptorSet{};
    } gradientMap;

    VulkanBuffer patch;
    uint32_t numPatches{25};
    uint32_t numTiles{121};
    FFT fft;
    VulkanBuffer screenQuad;
    Action* debugAction{};
    bool generateHComp{true};

    struct {
        VulkanBuffer circleBuffer;
        VulkanBuffer windArrow;
        glm::mat4 model{1};
        ImTextureID textureId{};
        Texture ColorBuffer;
        Offscreen::RenderInfo renderInfo;
    } windControl;

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VulkanDescriptorSetLayout textureDescriptorSetLayout;

    Scene scene;
    AtmosphereDescriptor atmosphere;

    struct DebugInfo {
        glm::vec4 N;
        glm::vec4 V;
        glm::vec4 R;
        glm::ivec4 counters;
    };

    VulkanBuffer debugBuffer;
    std::span<DebugInfo> debugInfo{};
    NormalMapping normalMapping;

    Offscreen offscreen;
    float sunZenith{7.5};
    float sunAzimuth{128};
};