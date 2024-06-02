#include "VulkanBaseApp.h"
#include "Floor.hpp"

struct VoxelData {
    glm::mat4 worldToVoxelTransform{1};
    glm::mat4 voxelToWordTransform{1};
    int numVoxels{};
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

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipelines();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderModel(VkCommandBuffer commandBuffer);

    void renderVoxels(VkCommandBuffer commandBuffer);

    void rayMarch(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void voxelize(VkCommandBuffer commandBuffer);

    void triangleParallelVoxelization(VkCommandBuffer commandBuffer);

    void fragmentParallelVoxelization(VkCommandBuffer commandBuffer);

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
        } rayMarch;
    } pipelines;

    Method method = Method::TriangleParallel;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    Floor floor;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
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

    RenderType renderType = RenderType::Default;
};