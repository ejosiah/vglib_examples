#include "VulkanBaseApp.h"
#include "spectrum/spectrum.hpp"

class SpectralPowerDistribution : public VulkanBaseApp{
public:
    explicit SpectralPowerDistribution(const Settings& settings = {});

protected:
    void initApp() override;

    void loadSpd();

    void initCamera(float height);

    void creatPatch();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } background;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;

    Camera camera;
    VulkanBuffer isolinePatch;
    VulkanBuffer spdValuesBuffer;
    VulkanBuffer spdWaveLengthBuffer;
    VulkanBuffer mvpBuffer;
    VulkanBuffer quadBuffer;
    VulkanDescriptorSetLayout spdDescriptorSetLayout;
    VkDescriptorSet spdDescriptorSet;

    spectrum::Spd spd;

    struct {
        glm::vec4 color;
        glm::vec2 resolution{};
        float minValue{};
        float maxValue{};
        float minWaveLength{};
        float maxWaveLength{};
        int numBins{};
        int lineResolution{};
    } spdConstants;


    struct {
        glm::ivec2 outerTessLevels{1, 0};
    } constants;
};