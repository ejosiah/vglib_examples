#include "FixedUpdate.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "fluid/FluidSolver2.hpp"
#include "BoundingBox.hpp"
#include "Floor.hpp"

using TemperatureAndDensity3D = eular::Quantity;

class Smoke3D : public VulkanBaseApp{
public:
    explicit Smoke3D(const Settings& settings = {});

protected:
    void initApp() override;

    void initSimData();

    void initSolver();

    eular::ExternalForce buoyancyForce();

    eular::ExternalForce periodicWindForce();

    std::vector<glm::vec4> initTemperatureAndDensityField();

    void initCamera();

    void initFloor();

    void createCollider();

    void initObstacleCollider();

    uint32_t createFieldDescriptorSet(std::vector<VkWriteDescriptorSet>& writes, uint32_t writeOffset, eular::Field& field);

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

    void renderFloor(VkCommandBuffer commandBuffer);

    void renderObstacle(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void renderVectorField(VkCommandBuffer commandBuffer);

    void clearTemperatureSum(VkCommandBuffer commandBuffer);

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

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } vector;

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

    struct WindControls {
        bool enabled{false};
        float angle{};
        float height{0.5f};
        float distance{0.7f};
        float radius{0.35f};
        float strength{12.0f};
        float period{2.0f};
        float pulseMin{0.1f};
    } windControls;

    struct WindConstants {
        glm::vec4 positionRadius{0, 0, 0, 0.35f};
        glm::vec4 directionStrength{-1, 0, 0, 12.0f};
        float time{};
        float period{2.0f};
        uint32_t enabled{};
        float pulseMin{0.1f};
    } windConstants;

    struct  {
        glm::vec3 position{0.5, 0.3, 0.5};
        float radius{0.075};
    } obstacle{};

    struct SimData {
        glm::mat4 worldToVoxel{1};
        glm::mat4 voxelToWorld{1};
        BoundingBox domain{{0, 0, 0}, {1, 2, 1}};
        BoundingBox emitterBounds{{0.45, -1, 0.45},{0.55, 0.05, 0.55}};
        glm::ivec3 resolution{50, 100, 50};
        glm::vec3 up{0, 1, 0};
        float ambientTemp{};
        float tempSum{};
        float minValue{0};
        float maxValue{1};
        float tempFactor{5.0};
        float densityFactory{-0.000625};
        float smokeDecayFactor{0.001};
        float temperatureDecayFactor{0.001};
        uint32_t numCells{1};
    } simData{};

    VulkanBuffer simDataBuffer;
    VulkanDescriptorSetLayout simDescriptorSetLayout;
    VkDescriptorSet simDescriptorSet{};
    std::vector<VulkanDescriptorSetLayout> sourceFieldSetLayouts;
    std::vector<VulkanDescriptorSetLayout> forceFieldSetLayouts;
    eular::Field obstacleColliderField;
    eular::Field obstacleColliderVelocityField;

    Floor floor;

    const glm::mat4 unitCubeToVoxel{toLocalSpace({ glm::vec3{-1}, glm::vec3{1} })};
    bool showOutline{};
    bool showVectorField{};

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } sphere;
    uint32_t numCells;
};

