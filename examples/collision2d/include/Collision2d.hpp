#include "VulkanBaseApp.h"
#include "FixedUpdate.hpp"
#include "Canvas.hpp"
#include "Sort.hpp"
#include "PrefixSum.hpp"

struct Domain{
    glm::vec2 lower{};
    glm::vec2 upper{};
};

struct GlobalData {
    glm::mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float halfSpacing;
    float time;
    uint32_t numObjects;
    uint32_t numCells;
    uint32_t segmentSize;
    uint32_t numCellIndices;
    uint32_t numEmitters;
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

struct EmitterData {
    glm::vec2 origin;
    glm::vec2 direction;
    int maxNumberOfParticlePerSecond;
    int maxNumberOfParticles;
    int numberOfEmittedParticles;
    float firstFrameTimeInSeconds;
    float currentTime;
    float speed;
    float spreadAngleRad;
    float radius;
    float offset;
    int disabled;
};


class Collision2d : public VulkanBaseApp {
public:
    explicit Collision2d(const Settings& settings = {});

protected:
    void initApp() override;

    void initScratchBuffer();

    void initObjects();

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

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void collisionDetection(VkCommandBuffer commandBuffer);

    void initializeCellIds(VkCommandBuffer commandBuffer);

    void sortCellIds(VkCommandBuffer commandBuffer);

    void countCells(VkCommandBuffer commandBuffer);

    void generateCellIndexArray(VkCommandBuffer commandBuffer);

    void compactCellIndexArray(VkCommandBuffer commandBuffer);

    void renderObjects(VkCommandBuffer commandBuffer);

    void renderGrid(VkCommandBuffer commandBuffer);

    void addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addComputeToTransferBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addComputeToTransferReadBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addTransferToComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

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
    } compute;

    struct  {
        const int maxParticles{200000};
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
    } objects;
    VulkanBuffer prevCellIds;
    VulkanBuffer prevAttributes;

    ScratchPad scratchPad;

    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet;

    VulkanDescriptorSetLayout stagingSetLayout;
    VkDescriptorSet stagingDescriptorSet;

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
        bool show{false};
    } grid;

    FixedUpdate fixedUpdate{60};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    RadixSort sort;
    PrefixSum prefixSum;
};