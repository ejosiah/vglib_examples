#include "VulkanBaseApp.h"
#include "Floor.hpp"

struct VoxelData {
    glm::mat4 worldToVoxelTransform{1};
    glm::mat4 voxelToWordTransform{1};
    int numVoxels{};
    int maxVoxels{};
};

struct HybridStats {
    uint32_t triangleIndexCount{};
    uint32_t fragmentIndexCount{};
    uint32_t triangleCount{};
    uint32_t fragmentCount{};
};

class Voxelization : public VulkanBaseApp {
public:
    enum class Method : int { TriangleParallel = 0, FragmentParallel, Hybrid };

    enum class RenderType : int {
        Default, Voxels, RayMarch
    };

    explicit Voxelization(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initFloor();

    void loadModel();

    void initCube();

    void initVoxelData();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createVoxelStorage();

    void updateVoxelDescriptorSet();

    void createHybridClassificationBuffers();

    void updateHybridClassifierDescriptorSet();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipelines();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    void beforeDeviceCreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderModel(VkCommandBuffer commandBuffer);

    void renderVoxels(VkCommandBuffer commandBuffer);

    void rayMarch(VkCommandBuffer commandBuffer);

    void updateUI();

    void renderUI(VkCommandBuffer commandBuffer);

    void clearVoxels(VkCommandBuffer commandBuffer);

    void voxelize(VkCommandBuffer commandBuffer);

    void triangleParallelVoxelization(VkCommandBuffer commandBuffer);

    void triangleParallelVoxelization(VkCommandBuffer commandBuffer, const VulkanBuffer& indices, uint32_t indexCount);

    void fragmentParallelVoxelization(VkCommandBuffer commandBuffer);

    void fragmentParallelVoxelization(VkCommandBuffer commandBuffer, const VulkanBuffer& indices, uint32_t indexCount);

    void hybridVoxelization(VkCommandBuffer commandBuffer);

    void classifyHybridTriangles(VkCommandBuffer commandBuffer);

    void triangleParallelVoxelizationIndirect(VkCommandBuffer commandBuffer);

    void fragmentParallelVoxelizationIndirect(VkCommandBuffer commandBuffer);

    static glm::mat4 fpMatrix(glm::ivec3 voxelDim);

    void generateVoxelTransforms(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } triangle;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } fragment;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } render;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } genVoxelTransforms;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } hybridClassifier;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } rayMarch;
    } pipelines;

    Method method = Method::FragmentParallel;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    Floor floor;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
        struct {
            VulkanDescriptorSetLayout descriptorSetLayout;
            VkDescriptorSet descriptorSet{};
            VulkanBuffer triangleIndices;
            VulkanBuffer fragmentIndices;
            VulkanBuffer stats;
            VulkanBuffer drawCommands;
            HybridStats* statsData{};
            uint32_t sourceTriangleCount{};
            float cutoffArea{4.0f};
            bool dirty{true};
        } hybrid;
    } model;

    struct {
        Texture texture;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        VkDescriptorSet transformsDescriptorSet{};
        glm::mat4 transform;
        VulkanBuffer transforms;
        VoxelData* data;
        VulkanBuffer dataBuffer;
        uint32_t size{256};
    } voxels;


    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cube;

    struct  {
        glm::vec4 min{1};
        glm::vec4 max{1};
    } bounds;

    bool refreshVoxel{true};
    bool recreateVoxelStorage{};

    RenderType renderType = RenderType::Default;
};
