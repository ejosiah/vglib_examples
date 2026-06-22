#include "FixedUpdate.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "fluid/FluidSolver2.hpp"
#include "BoundingBox.hpp"

using TemperatureAndDensity3D = eular::Quantity;

class Smoke3D : public VulkanBaseApp{
public:
    explicit Smoke3D(const Settings& settings = {});

protected:
    void initApp() override;

    void initSimData();

    void initSolver();

    eular::ExternalForce buoyancyForce();

    std::vector<glm::vec4> initTemperatureAndDensityField();

    void initCamera();

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

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderDomain(VkCommandBuffer commandBuffer);

    void renderEmitter(VkCommandBuffer commandBuffer);

    void renderSmoke(VkCommandBuffer commandBuffer);

    void emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    bool decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    void updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    std::vector<PipelineMetaData> pipelines();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } rayMarch;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    TemperatureAndDensity3D temperatureAndDensity;
    std::unique_ptr<eular::FluidSolver> fluidSolver;
    ComputePipelines compute;
    FixedUpdate fixedUpdate;

    struct SimData {
        glm::mat4 worldToVoxel{1};
        glm::mat4 voxelToWorld{1};
        BoundingBox domain{{0, 0, 0}, {1, 2, 1}};
        BoundingBox emitterBounds{{0.45, -1, 0.45},{0.55, 0.05, 0.55}};
        glm::ivec3 resolution{50, 100, 50};
        glm::vec3 up{0, 1, 0};
        float ambientTemp{};
        float minValue{0};
        float maxValue{1};
        float tempFactor{5.0};
        float densityFactory{-0.000625};
        uint32_t numCells{1};
    } simData;

    VulkanBuffer simDataBuffer;
    VulkanBuffer ambientTemperaturePartialSums;
    glm::uvec2 ambientTemperatureGroupCount{1};
    VulkanDescriptorSetLayout simDescriptorSetLayout;
    VkDescriptorSet simDescriptorSet{};
    std::vector<VulkanDescriptorSetLayout> sourceFieldSetLayouts;
    std::vector<VulkanDescriptorSetLayout> forceFieldSetLayouts;

    const glm::mat4 unitCubeToVoxel{toLocalSpace({ glm::vec3{-1}, glm::vec3{1} })};

};

