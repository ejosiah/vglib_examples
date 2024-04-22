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
    int numObjects;
    int numCells;
    int screenWidth;
    int screenHeight;
    int segmentSize;
};

struct Attribute {
    uint32_t objectID;
    uint32_t controlBits;
};

class Collision2d : public VulkanBaseApp {
public:
    explicit Collision2d(const Settings& settings = {});

protected:
    void initApp() override;

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

    void renderObjects(VkCommandBuffer commandBuffer);

    void renderGrid(VkCommandBuffer commandBuffer);

    void addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addComputeToTransferBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addTransferToComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        Pipeline object;
        Pipeline objectCenter;
        Pipeline objectBoundingBox;
    } render;

    struct {
        Pipeline initCellIDs;
        Pipeline countCells;
    } compute;

    struct  {
        const int maxParticles{200000};
        VulkanBuffer position;
        VulkanBuffer velocity;
        VulkanBuffer radius;
        VulkanBuffer cellIds;
        VulkanBuffer counts;
        VulkanBuffer offsets;
        VulkanBuffer attributes;
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
    } objects;

    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet;

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