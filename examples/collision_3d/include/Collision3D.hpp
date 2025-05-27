#include "gltf/GltfLoader.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"
#include "model.hpp"
#include "ComputePipelins.hpp"
#include "FixedUpdate.hpp"
#include "Sort.hpp"
#include "PrefixSum.hpp"

namespace DebugType {
    static constexpr int CELL_TYPE = 0;
}

class Collision3D : public VulkanBaseApp {
public:
    explicit Collision3D(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void setDomain();

    void initObjects();

    void initSort();

    void createGizmo();

    void initDebug();

    void initParticleEmitters();

    void initSphereEmitters();

    void createShapes();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createInverseCam();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderGizmo(VkCommandBuffer commandBuffer);

    void renderGrid(VkCommandBuffer commandBuffer);

    void renderBounds(VkCommandBuffer commandBuffer);

    void renderParticles(VkCommandBuffer commandBuffer);

    void runSimulation(VkCommandBuffer commandBuffer);

    void emitParticles(VkCommandBuffer commandBuffer);

    void integrate(VkCommandBuffer commandBuffer);

    void solveConstraints(VkCommandBuffer commandBuffer);

    void processCollisions(VkCommandBuffer commandBuffer);

    void computeDispatch(VkCommandBuffer commandBuffer, uint32_t objectType);

    void initializeCellIds(VkCommandBuffer commandBuffer);

    void sortCellIds(VkCommandBuffer commandBuffer);

    void countCells(VkCommandBuffer commandBuffer);

    void generateCellIndexArray(VkCommandBuffer commandBuffer);

    void compactCellIndexArray(VkCommandBuffer commandBuffer);

    void resolveCollision(VkCommandBuffer commandBuffer);

    void solveConstraint(Pipeline& pipeline, VkCommandBuffer commandBuffer);

    void applyCorrection(VkCommandBuffer commandBuffer);

    void updateVelocity(VkCommandBuffer commandBuffer);

    void checkBounds(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void initScratchBuffer();

    BufferRegion reserve(VkDeviceSize size);

    void endFrame() override;

    uint32_t gridSize();

protected:
    static constexpr uint32_t workGroupSize = 256;

    struct {
        Pipeline flat;
        Pipeline bounds;
        Pipeline shape;
        Pipeline grid;
    } render;

    struct {
        Pipeline emitter;
        Pipeline sphereEmitter;
        Pipeline integrate;
        Pipeline correction;
        Pipeline velocity;
        Pipeline boundsCheck;
        Pipeline computeDispatch;
        Pipeline initCellIDs;
        Pipeline countCells;
        Pipeline generateCellIndexArray;
        Pipeline compactCellIndexArray;
        Pipeline collisionTest;
        std::vector<Pipeline> constraints;
    } compute;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    VulkanBuffer inverseCamProj;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } bounds;

    struct {
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } solid;
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } outline;
    } cell;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } ball;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Domain domain{};

    struct {
        VulkanBuffer gpu;
        GlobalData* cpu{};
    } globals;

    struct  {
        const int maxParticles{51000};
        VulkanBuffer indices;
        std::array<VulkanBuffer, 2> position;
        VulkanBuffer velocity;
        VulkanBuffer correctionVector;
        VulkanBuffer radius;
        VulkanBuffer cellIds;
        VulkanBuffer counts;
        VulkanBuffer attributes;
        VulkanBuffer cellIndexArray;
        BufferRegion cellIndexStaging;
        BufferRegion bitSet;
        BufferRegion compactIndices;
        VulkanBuffer dispatchBuffer;
        struct {
            VulkanBuffer distance;
        } constraints;
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
        const float defaultRadius{0.06};
        uint32_t gridSize{};
    } objects;
    VulkanBuffer prevCellIds;
    VulkanBuffer prevAttributes;

    struct  {
        VulkanBuffer particle;
        VulkanBuffer sphere;
    } emitters;

    FixedUpdate fixedUpdate{120};
    ScratchPad scratchPad;
    bool pauseSim{};
    bool displayStatus{};

    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet{};

    VulkanDescriptorSetLayout emitterSetLayout;
    VkDescriptorSet emitterDescriptorSet{};

    VulkanDescriptorSetLayout stagingSetLayout;
    VkDescriptorSet stagingDescriptorSet;

    struct DebugInfo {
        std::array<glm::vec3, 8> center{};
        std::array<glm::vec3, 8> min{};
        std::array<glm::vec3, 8> max{};
        std::array<int, 8> overlap{};
    };

    struct {
        bool enabled{false};
        bool outline{true};
        int type{DebugType::CELL_TYPE};
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        std::array<std::span<glm::vec3>, 2> positions;
        std::span<float> radius;
        std::span<uint32_t> counts;
        std::span<DebugInfo> info;
        VulkanBuffer buffer;
    } debug;

    struct {
        VulkanBuffer vertices;
        glm::mat4 transform{1};
    } gizmo;
    glm::mat4 identity{1};
    RadixSort sort;
    PrefixSum prefixSum;
    Action* pauseAction{};
    bool pauseRequested{};
    Action* statusAction{};
};