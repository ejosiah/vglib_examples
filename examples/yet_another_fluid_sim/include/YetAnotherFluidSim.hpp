#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"

#include "CpuFluidSolver.hpp"
#include "FixedUpdate.hpp"
#include "fluid/FluidSolver2.hpp"
#include "fluid/FieldVisualizer.hpp"

#include <map>
#include <optional>
#include <vector>

using Smoke = eular::Quantity;

enum class Scene : uint32_t { Tank, WindTunnel, Paint };

struct SceneProps {
    glm::vec4 color{0.8, 0.8, 0.8, 1.0};
    uint32_t resolution{100};
    uint32_t iterations{40};
    float timeStep{0.01666667};
    float gravity{-9.81};
    bool smoke{};
    bool pressure{};
    bool streamLines{};

};

class YetAnotherFluidSim : public VulkanBaseApp{
public:
    explicit YetAnotherFluidSim(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initSceneProperties();

    void initSolver();

    void initVisualizer();

    void initSmoke();

    eular::ExternalForce force();

    void initCpuSolver();

    void updateCpuSolver();

    void copyCpuFieldsToUploadBuffers();

    void uploadCpuFields(VkCommandBuffer commandBuffer);

    void uploadCpuField(VkCommandBuffer commandBuffer, const VulkanBuffer& buffer, eular::Field& field);

    void initObstacleCollider();

    void updateObstacleCollider(VkCommandBuffer commandBuffer);

    void updatePaintSmoke(VkCommandBuffer commandBuffer);

    void runSimulationStep(VkCommandBuffer commandBuffer);

    uint32_t createFieldDescriptorSet(std::vector<VkWriteDescriptorSet>& writes, uint32_t writeOffset, eular::Field& field);

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createComputePipeline();

    void createRenderPipeline();

    void renderSmoke(VkCommandBuffer commandBuffer);

    void renderObstacle(VkCommandBuffer commandBuffer);

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer cmdBuf);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        glm::mat4 transform{1};
        glm::vec4 color{0.98f, 0.45f, 0.0f, 1.0f};
        glm::vec2 position{0.36f, 0.51f};
        glm::vec2 velocity{};
        glm::vec2 domainMin{0.0f};
        glm::vec2 domainMax{1.0f};
        float radius{0.15f};
        float size{1.0f};
        uint32_t scene{};
    } obstacleConstants;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } obstacleRender;

    struct {
        uint32_t frame{};
    } paintSmokeSourceConstants;

    eular::Field obstacleColliderField;
    eular::Field obstacleColliderVelocityField;
    std::vector<VulkanDescriptorSetLayout> obstacleColliderSetLayouts;
    std::vector<VulkanDescriptorSetLayout> paintSmokeSetLayouts;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    Scene scene{Scene::Paint};
    Scene newScene{Scene::Paint};

    bool advance{};
    FixedUpdate fixedUpdate;
    bool obstacleDragActive{};
    bool leftMouseWasHeld{};
    glm::vec2 obstacleDragOffset{};
    glm::vec2 obstacleSimulationPosition{};

    std::map<Scene, SceneProps> scenes;

    glm::uvec2 solverGridSize{1};
    FieldVisualizer visualizer;
    std::unique_ptr<eular::FluidSolver> solver;
    std::unique_ptr<CpuFluidSolver> cpuSolver;
    std::vector<float> cpuUUpload;
    std::vector<float> cpuVUpload;
    std::vector<float> cpuPressureUpload;
    VulkanBuffer cpuUUploadBuffer;
    VulkanBuffer cpuVUploadBuffer;
    VulkanBuffer cpuPressureUploadBuffer;

    ComputePipelines compute;
    std::vector<VulkanDescriptorSetLayout> forceFieldSetLayouts;

    glm::vec2 simSize{0.0, 1.1};
    glm::vec2 domainSize{0.0, 1.0};
    Smoke smoke;
    std::vector<float> smokeField;

    struct {
        glm::vec2 point{};
        glm::vec2 velocity{};
        glm::vec2 domainMin{0.0f};
        glm::vec2 domainMax{1.0f};
        float speed{};
        uint32_t mode{0};
        float radius{};
        float dt{1.0f};
    } forceConstants;

    bool showPressure{};
    bool showStreamLines{};
    bool showSmoke{};

};
