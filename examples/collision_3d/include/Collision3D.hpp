#include "gltf/GltfLoader.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayTraceBaseApp.hpp"
#include "shader_binding_table.hpp"
#include "model.hpp"
#include "ComputePipelins.hpp"
#include "FixedUpdate.hpp"

class Collision3D : public VulkanBaseApp {
public:
    explicit Collision3D(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initObjects();

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

    void initCanvas();

    void createInverseCam();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderBounds(VkCommandBuffer commandBuffer);

    void renderParticles(VkCommandBuffer commandBuffer);

    void runSimulation(VkCommandBuffer commandBuffer);

    void emitParticles(VkCommandBuffer commandBuffer);

    void integrate(VkCommandBuffer commandBuffer);

    void solveConstraints(VkCommandBuffer commandBuffer);

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

protected:
    struct {
        Pipeline bounds;
        Pipeline shape;
    } render;

    struct {
        Pipeline emitter;
        Pipeline sphereEmitter;
        Pipeline integrate;
        Pipeline correction;
        Pipeline velocity;
        Pipeline boundsCheck;
        std::vector<Pipeline> constraints;
    } compute;

    ShaderTablesDescription shaderTablesDesc;
    ShaderBindingTables bindingTables;

    VulkanBuffer inverseCamProj;
    Canvas canvas{};

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } bounds;

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
    Domain3D domain{};

    struct {
        VulkanBuffer gpu;
        GlobalData3D* cpu{};
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
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
        const float defaultRadius{0.05};
        uint32_t gridSize{};
    } objects;
    VulkanBuffer prevCellIds;
    VulkanBuffer prevAttributes;

    struct  {
        VulkanBuffer particle;
        VulkanBuffer sphere;
    } emitters;

    FixedUpdate fixedUpdate{480};
    ScratchPad3D scratchPad;
    bool pauseSim{true};

    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet{};

    VulkanDescriptorSetLayout emitterSetLayout;
    VkDescriptorSet emitterDescriptorSet{};

};