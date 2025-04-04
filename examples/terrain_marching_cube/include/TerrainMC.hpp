#define MAX_IN_FLIGHT_FRAMES 1
#include "TerrainCompute.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "ResourcePool.hpp"
#include "Models.hpp"
#include "gpu/HashSet.hpp"
#include "Sort.hpp"

struct DebugConstants {
    glm::ivec3 dim{256, 8, 256};
//    glm::ivec3 dim{16, 2, 16};
    float elapsed_time{};
};

struct VoxelGenConstants {
    uint32_t pass{};
    uint32_t blocksPerPass{};
};

static constexpr float BLOCK_SIZE = 4;
static constexpr float HALF_BLOCK_SIZE = BLOCK_SIZE * 0.5;

class TerrainMC : public VulkanBaseApp {
public:
    explicit TerrainMC(const Settings& settings = {});

protected:
    void initApp() override;

    void initDebugStuff();

    void checkInvariants();

    void initHelpers();

    void initCamera();

    void initLuts();

    void initBindlessDescriptor();

    void createCube();

    void initBlockData();

    void initVoxels();

    void createNoiseTextures();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderCamera(VkCommandBuffer commandBuffer);

    void renderCube(VkCommandBuffer commandBuffer, Camera& acamera, const glm::mat4& model, bool outline = false);

    void renderBlocks(VkCommandBuffer commandBuffer, const Camera& camera);

    void renderGridBounds(VkCommandBuffer commandBuffer, const Camera& camera);

    void renderScene(VkCommandBuffer commandBuffer, const Camera& camera);

    void renderDebug(VkCommandBuffer commandBuffer);

    void newFrame() override;

    void update(float time) override;

    void checkAppInputs() override;

    void updateVisibilityList();

    void cleanup() override;

    void onPause() override;

    void computeCameraBounds();

    void generateTerrain();

    void generateTerrain(VkCommandBuffer commandBuffer);

    void prepareBuffers(VkCommandBuffer commandBuffer);

    void sortDrawCommands(VkCommandBuffer commandBuffer);

    void sortBlocks(VkCommandBuffer commandBuffer);

    void debugScene();

    void computeToComputeBarrier(VkCommandBuffer commandBuffer);

    void transferToComputeBarrier(VkCommandBuffer commandBuffer);

    void transferToInputAssembly(VkCommandBuffer commandBuffer);

    void computeToTransferBarrier(VkCommandBuffer commandBuffer);

    void computeReadToComputeWriteBarrier(VkCommandBuffer commandBuffer);

    void computeToRenderBarrier(VkCommandBuffer commandBuffer);

    void blockInCameraTest(VkCommandBuffer commandBuffer);

    void generateBlocks(VkCommandBuffer commandBuffer);

    void computeDrawBlocks(VkCommandBuffer commandBuffer);

    void generateTextures(VkCommandBuffer commandBuffer, int pass);

    void generateVoxels(VkCommandBuffer commandBuffer);

    void generateNoise();

    void marchTextures(VkCommandBuffer commandBuffer, int pass);

    void generateBlocks();

    void computeDistanceToCamera(VkCommandBuffer commandBuffer);

    void copyBuffersToCpu(VkCommandBuffer commandBuffer);


protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } cubeRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } camRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } blockRender;

    struct {
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } solid;
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } outline;
        std::vector<glm::mat4> instances;
    } cube;

    struct {
        VulkanBuffer vertices;
        glm::mat4 transform{1};
        glm::mat4 aabb{1};
    } cameraBounds;

    std::vector<glm::mat4> visibleList;
    std::vector<glm::mat4> skipList;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<SpectatorCameraController> camera;
    Camera debugCamera{};
    BindlessDescriptor bindlessDescriptor;
    ResourcePool<glm::mat4> cubes;
    ResourcePool<std::tuple<glm::vec3, glm::vec3>> boundingBoxes;
    Vertices cBounds;

    glm::mat4 tinyCube;
    CameraInfo cameraInfo{};
    static const int poolSize{300};
    static const int scratchTextureCount{30};
    GpuData gpu;
    VulkanDescriptorSetLayout terrainDescriptorSetLayout;
    VulkanDescriptorSetLayout cameraDescriptorSetLayout;
    VulkanDescriptorSetLayout indirectDescriptorSetLayout;
    VulkanDescriptorSetLayout marchingCubeLutSetLayout;
    VulkanDescriptorSetLayout noiseDescriptorSetLayout;
    std::vector<VkDescriptorSet> cameraDescriptorSet;
    VkDescriptorSet terrainDescriptorSet;
    VkDescriptorSet indirectDescriptorSet;
    VkDescriptorSet marchingCubeLutSet;
    VkDescriptorSet noiseDescriptorSet;
    TerrainCompute compute;
    VkMemoryBarrier2 memoryBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    VkImageMemoryBarrier2 imageMemoryBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

    Counters* counters{};
    RadixSort sort;
    gpu::HashSet processed_blocks;
    gpu::HashSet empty_blocks;
    DebugConstants debugConstants;
    std::array<VkDescriptorSet, 4> gen_sets;
    VkDeviceSize debugDrawOffset{};
    VulkanBuffer cpuBuffer;
    VulkanBuffer gridBoundsBuffer;
    std::array<Vertex, 24> gridBounds;
    std::span<DrawCommand> drawCmds;
    std::span<DebugData> dd;
    std::span<float> distance_to_camera;
    VoxelGenConstants voxelGenConstants;
    glm::vec3 worldDim{128, 32, 256};
};