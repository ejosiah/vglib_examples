#include "VulkanBaseApp.h"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayQuerySupport.hpp"
#include "Canvas.hpp"

class RealTimeClouds : public VulkanBaseApp, public VulkanRayQuerySupport{
public:
    explicit RealTimeClouds(const Settings& settings = {});

protected:
    void initApp() override;

    void initCanvas();

    void initCamera();

    void createNoiseTexture();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void createAccelerationStructure();

    void updateAccelerationStructureDescriptorSet();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createNoiseGeneratorPipeline();

    void createVolumeRenderPipeline();

    void renderVolume();

    void generateNoise(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void renderSkyDome(VkCommandBuffer commandBuffer);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void updateSun();

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::mat4 mvp;
            alignas(16) glm::vec3 eyes;
            alignas(16) glm::vec3 sun{0, 0, 0};
        } constants;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
    } skyDome;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::mat4 mvp;
            alignas(16) glm::vec3 eyes;
            alignas(16) glm::vec3 sun{0, 0, 0};
        } constants;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
    } earth;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
    } cloudShell;

    static constexpr float EARTH_RADIUS = 6371 * km;
    static constexpr float CLOUD_MIN = 1.5 * km;
    static constexpr float CLOUD_MAX = 5.0 * km;
    static constexpr float SUN_DISTANCE = 100000 * km;
    static constexpr float MAX_HEIGHT = 8.849 * km;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } cloudGen;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline  pipeline;
        struct {
            alignas(16) glm::vec3 lightPosition;
            alignas(16) glm::vec3 viewPosition;
            float coverage{1};
            float precipitation{0};
            float cloudType{1};
            float time{0};
            float boxScale{1};
            float eccentricity{0.2};
        } constants;
    } volumeRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline  pipeline;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet;
        Texture texture;
        VulkanBuffer transferBuffer;
        int iterations = 9;
        bool on = false;
    } blur;

    bool updateState = true;

    Texture lowFrequencyNoiseTexture;
    Texture highFrequencyNoiseTexture;
    Texture weatherTexture;
    Texture curlNoiseTexture;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
//    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<FirstPersonCameraController> camera;

    VulkanDescriptorSetLayout noiseTextureSetLayout;
    VulkanDescriptorSetLayout noiseImageSetLayout;

    VkDescriptorSet noiseTextureSet;
    VkDescriptorSet noiseImageSet;

    static constexpr int NumNoiseSamples = 128;
    Canvas canvas{};

    VulkanDescriptorSetLayout volumeDescriptorSetLayout;
    VkDescriptorSet volumeDescriptorSet;
    std::vector<rt::Instance> asInstances;
    rt::AccelerationStructureBuilder accStructBuilder;
    VulkanBuffer inverseCamProj;

    struct {
        float azimuth{0};
        float elevation{0};
        float illuminance{120000};
        bool enabled{true};
    } sun;
};