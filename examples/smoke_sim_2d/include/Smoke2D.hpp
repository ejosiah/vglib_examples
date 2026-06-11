#include "VulkanBaseApp.h"
#include "fluid/FluidSolver2.hpp"
#include "fluid/FieldVisualizer.hpp"

using TemperatureAndDensity = eular::Quantity;

class Smoke2D : public VulkanBaseApp{
public:
    explicit Smoke2D(const Settings& settings = {});

protected:
    void initApp() override;

    void initFullScreenQuad();

    void initFieldVisualizer();

    void createDescriptorPool();

    void createDescriptorSet();

    void updateDescriptorSets();

    void initBoundaryTexture();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void initAmbientTempBuffer();

    void beforeDeviceCreation() override;

    void initTemperatureAndDensityField();

    void initSolver();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    bool decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    void updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    void renderTemperature(VkCommandBuffer commandBuffer);

    void renderSmoke(VkCommandBuffer commandBuffer);

    void renderBoundary(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    eular::ExternalForce buoyancyForce();

//#define toKelvin(celsius) (273.15f + celsius)
#define toKelvin(celsius) (celsius)


protected:
    static constexpr float MIN_TEMP = toKelvin(-20);  // celsius
    static constexpr float AMBIENT_TEMP = toKelvin(0); // celsius
    static constexpr float MAX_TEMP = toKelvin(100); // celsius
    static constexpr float TARGET_TEMP = toKelvin(260); // celsius
    static constexpr float TIME_STEP = 0.004166666666; // seconds
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            float minTemp{MIN_TEMP};
            float maxTemp{MAX_TEMP};
        } constants;
    } temperatureRender;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 location{0.5, 0.94};
            float tempTarget{TARGET_TEMP};
            float ambientTemp{AMBIENT_TEMP};
            float radius{0.0045};
            float tempRate{8};
            float densityRate{28};
            float decayRate{5};
            float dt{TIME_STEP};
            float time{0};
        } constants;
    } emitter;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 location{0.5, 0.94};
            float densityDecayRate{0.045};
            float temperatureDecayRate{0.02};
            float dt{TIME_STEP};
            float radius{0.0045};
        } constants;
    } smokeDecay;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } copyTemperatureField;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::vec3 dye{2.4, 4.15, 4.9};
        } constants;
    } smokeRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } boundaryRender;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 up{0, -1};
            float tempFactor{0.45};
            float densityFactory{0.08};
        } constants;
    } buoyancyForceGen;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    VulkanBuffer screenQuad;

    TemperatureAndDensity temperatureAndDensity;
    std::unique_ptr<eular::FluidSolver> fluidSolver;
    VulkanDescriptorSetLayout ambientTempSet;
    VkDescriptorSet ambientTempDescriptorSet;
    Texture boundaryTexture;
    VulkanDescriptorSetLayout computeBoundarySetLayout;
    VkDescriptorSet computeBoundaryDescriptorSet{};
    VulkanDescriptorSetLayout boundaryRenderSetLayout;
    VkDescriptorSet boundaryRenderDescriptorSet{};
    VulkanBuffer ambientTempBuffer;
    float* ambientTemp{};
    float* temps;
    VulkanBuffer tempField;
    VulkanBuffer debugBuffer;
    Action* toggleBoundary{};
    bool showBoundary{false};
    bool dynamicAmbientTemp{false};
    int fwidth{};
    FieldVisualizer fieldVisualizer;

    static constexpr int in{0};
    static constexpr int out{1};
};
