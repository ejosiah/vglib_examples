#include "VulkanBaseApp.h"
#include "FixedUpdate.hpp"
#include "Canvas.hpp"
#include "Sort.hpp"
#include "PrefixSum.hpp"

namespace Dispatch {
    static constexpr uint32_t Object = 0;
    static constexpr uint32_t CellID = 1;
    static constexpr uint32_t CellArrayIndex = 2;
    static constexpr uint32_t Count = 3;

    static constexpr uint32_t ObjectCmd = 0;
    static constexpr uint32_t CellIDCmd = sizeof(uint32_t) * 4;
    static constexpr uint32_t CellArrayIndexCmd = sizeof(uint32_t) * 8;
    static constexpr VkDeviceSize Size = sizeof(uint32_t) * 4 * Count;
};

struct Domain{
    glm::vec2 lower{};
    glm::vec2 upper{};
};

struct UpdateInfo {
    uint32_t objectId;
    uint32_t pass;
    uint32_t tid;
    uint32_t cellID;
};


struct GlobalData {
    glm::mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float halfSpacing;
    float time;
    uint32_t numObjects;
    uint32_t gridSize;
    uint32_t numCells;
    uint32_t segmentSize;
    uint32_t numCellIndices;
    uint32_t numEmitters;
    uint32_t numUpdates;
    uint32_t frame;
    uint32_t screenWidth;
    uint32_t screenHeight;
};

struct Attribute {
    uint32_t objectID;
    uint32_t controlBits;
};

struct CellInfo {
    uint32_t index;
    uint32_t numHomeCells;
    uint32_t numPhantomCells;
    uint32_t numCells;
};

struct ScratchPad {
    VulkanBuffer buffer;
    VkDeviceSize offset{0};
};

struct Emitter {
    glm::vec2 origin;
    glm::vec2 direction;
    float radius;
    float offset;
    float speed;
    float spreadAngleRad;
    int maxNumberOfParticlePerSecond;
    int maxNumberOfParticles;
    float firstFrameTimeInSeconds;
    float currentTime;
    int numberOfEmittedParticles;
    int disabled;
};


class Collision2d : public VulkanBaseApp {
public:
    explicit Collision2d(const Settings& settings = {});

protected:
    void initApp() override;

    void initEmitters();

    void initScratchBuffer();

    void initObjects();

    void initObjectsForDebugging();

    void initGrid();

    void initSort();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    void runDebug();

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void collisionDetection(VkCommandBuffer commandBuffer);

    void initializeCellIds(VkCommandBuffer commandBuffer);

    void sortCellIds(VkCommandBuffer commandBuffer);

    void countCells(VkCommandBuffer commandBuffer);

    void generateCellIndexArray(VkCommandBuffer commandBuffer);

    void compactCellIndexArray(VkCommandBuffer commandBuffer);

    void resolveCollision(VkCommandBuffer commandBuffer);

    void bruteForceResolveCollision(VkCommandBuffer commandBuffer);

    void emitParticles(VkCommandBuffer commandBuffer);

    void boundsCheck(VkCommandBuffer commandBuffer);

    void integrate(VkCommandBuffer commandBuffer);

    void computeDispatch(VkCommandBuffer commandBuffer, uint32_t objectType);

    void renderObjects(VkCommandBuffer commandBuffer);

    void renderGrid(VkCommandBuffer commandBuffer);

    void addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addDispatchBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addComputeToTransferBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addComputeToTransferReadBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addTransferToComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    BufferRegion reserve(VkDeviceSize size);

protected:
    struct {
        Pipeline object;
        Pipeline objectCenter;
        Pipeline objectBoundingBox;
    } render;

    struct {
        Pipeline initCellIDs;
        Pipeline countCells;
        Pipeline generateCellIndexArray;
        Pipeline compactCellIndexArray;
        Pipeline collisionTest;
        Pipeline bruteForceTest;
        Pipeline computeDispatch;
        Pipeline emitter;
        Pipeline integrate;
        Pipeline boundsCheck;
    } compute;

    struct  {
        const int maxParticles{20000};
        VulkanBuffer indices;
        VulkanBuffer position;
        VulkanBuffer velocity;
        VulkanBuffer radius;
        VulkanBuffer colors;
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
    VulkanBuffer emitters;
    static constexpr VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;

    VulkanDescriptorSetLayout emitterSetLayout;
    VkDescriptorSet emitterDescriptorSet{};

    ScratchPad scratchPad;

    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet;

    VulkanDescriptorSetLayout stagingSetLayout;
    VkDescriptorSet stagingDescriptorSet;
    VulkanBuffer updatesBuffer;

    struct {
        VulkanBuffer gpu;
        GlobalData* cpu{};
    } globals;

    struct {
        Canvas canvas;
        struct {
            Domain domain;
            float spacing{};
        } constants;
        bool show{true};
    } grid;

    FixedUpdate fixedUpdate{480};
    uint32_t iterations{8};
    bool debugMode{false};
    static constexpr uint32_t workGroupSize = 256;
    uint32_t frameStart = 0;
    bool useBruteForce = false;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    RadixSort sort;
    PrefixSum prefixSum;
};