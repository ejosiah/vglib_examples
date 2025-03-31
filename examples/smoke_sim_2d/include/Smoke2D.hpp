#include "VulkanBaseApp.h"
#include "fluid_solver_2d.h"
#include "fluid/FluidSolver2.hpp"

using TemperatureAndDensity = Quantity;
using TemperatureAndDensity1 = eular::Quantity;

class Smoke2D : public VulkanBaseApp{
public:
    explicit Smoke2D(const Settings& settings = {});

protected:
    void initApp() override;

    void initFullScreenQuad();

    void createDescriptorPool();

    void createDescriptorSet();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void initAmbientTempBuffer();

    void beforeDeviceCreation() override;

    void initTemperatureAndDensityField();

    void initTemperatureAndDensityField1();

    void initSolver();

    void copy(VkCommandBuffer commandBuffer, Texture& source, const VulkanBuffer& destination);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void emitSmoke(VkCommandBuffer commandBuffer, Field &field);

    void emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    bool decaySmoke(VkCommandBuffer commandBuffer, Field &field);

    bool decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);


    void updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    void renderTemperature(VkCommandBuffer commandBuffer);

    void renderSmoke(VkCommandBuffer commandBuffer);

    void renderSource(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    ExternalForce buoyancyForce();

    eular::ExternalForce buoyancyForce1();


#define toKelvin(celsius) (273.15f + celsius)
//#define toKelvin(celsius) (celsius)


protected:
    static constexpr float MIN_TEMP = toKelvin(-20);  // celsius
    static constexpr float AMBIENT_TEMP = toKelvin(0); // celsius
    static constexpr float MAX_TEMP = toKelvin(100); // celsius
    static constexpr float TARGET_TEMP = toKelvin(150); // celsius
    static constexpr float TIME_STEP = 0.004166666666; // seconds
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            float minTemp{MIN_TEMP};
            float maxTemp{MAX_TEMP};
        } constants;
    } temperatureRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 location{0.5, 0.98};
            float tempTarget{TARGET_TEMP};
            float ambientTemp{AMBIENT_TEMP};
            float radius{0.001};
            float tempRate{0.5}; // 1
            float densityRate{0.3};
            float decayRate{5};
            float dt{TIME_STEP};
            float time{0};
        } constants;
    } emitter;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 location{0.5, 0.98};
            float densityDecayRate{0.1};
            float temperatureDecayRate{0.001};
            float dt{TIME_STEP};
            float radius{0.001};
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
            glm::vec3 dye{2, 255, 255};
        } constants;
    } smokeRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } compute;
        struct{
            glm::vec2 up{0, -1};
            float tempFactor{1}; // 0.1
            float densityFactory{0};
        } constants;
    } buoyancyForceGen;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    VulkanBuffer screenQuad;

    TemperatureAndDensity temperatureAndDensity;
    TemperatureAndDensity1 temperatureAndDensity1;
    FluidSolver2D fluidSolver;
    eular::FluidSolver fluidSolver1;
    VulkanDescriptorSetLayout ambientTempSet;
    VkDescriptorSet ambientTempDescriptorSet;
    VulkanBuffer ambientTempBuffer;
    float* ambientTemp{};
    float* temps;
    VulkanBuffer tempField;
    VulkanBuffer debugBuffer;
    bool dynamicAmbientTemp{false};
    int fwidth{};

    static constexpr int in{0};
    static constexpr int out{1};
};