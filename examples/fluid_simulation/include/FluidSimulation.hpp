#include "VulkanBaseApp.h"
#include "fluid_solver_2d.h"
#include "fluid/FluidSolver2.hpp"


using ColorField = Field;

class FluidSimulation : public VulkanBaseApp{
public:
    explicit FluidSimulation(const Settings& settings = {});

protected:
    void initApp() override;

    void initFluidSolver();

    void initColorField();

    void initColorQuantity();

    void initFullScreenQuad();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void createCommandPool();

    void createPipelineCache();

    void createComputePipeline();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderColorField(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void runSimulation();

    ExternalForce userInputForce();

    eular::ExternalForce userInputForce2();

    void addDyeSource(VkCommandBuffer commandBuffer, Field& field, glm::vec3 color, glm::vec2 source);

    void addDyeSource1(VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc, glm::vec3 color, glm::vec2 source);

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void createSamplers();

    void beforeDeviceCreation() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;

    VulkanDescriptorSetLayout textureSetLayout;
    struct {
        VulkanBuffer vertices;
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } screenQuad;

    struct ForceConstants {
        glm::vec2 force{0};
        glm::vec2 center{0};
        float radius{1};
        float dt;
    };

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        ForceConstants constants;
    } forceGen;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
        ForceConstants constants;
    } forceGen2;

    struct {
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;

        struct {
            VulkanPipeline pipeline;
            VulkanPipelineLayout layout;
        } compute;
        struct{
            glm::vec3 color{1};
            glm::vec2 source;
            float radius{0.01};
            float dt{1.0f/120.f};
        } constants;
    } dyeSource;

    static const int in{0};
    static const int out{1};

    Quantity color;
    eular::Quantity color1;

    struct {
        float dt{1.0f / 120.f};
        float epsilon{1};
        float rho{1};
    } constants;

    VulkanBuffer debugBuffer;
    VulkanSampler valueSampler;
    VulkanSampler linearSampler;
    FluidSolver2D fluidSolver;
    eular::FluidSolver fluidSolver2;
    float diffuseRate = 0;
};