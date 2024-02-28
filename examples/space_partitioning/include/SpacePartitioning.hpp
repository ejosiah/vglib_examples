#include "VulkanBaseApp.h"
#include "domain.hpp"

class SpacePartitioning : public VulkanBaseApp{
public:
    explicit SpacePartitioning(const Settings& settings = {});

protected:
    void initApp() override;

    void initQuery();

    void createPoints();

    void generatePoints();

    void initCamera();

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

    void renderPoints(VkCommandBuffer commandBuffer);

    void renderSpitAxis(VkCommandBuffer commandBuffer);

    void renderKdTree(VkCommandBuffer commandBuffer);

    void renderSearchArea(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void runGpuSearch();

    void runCpuSearch();

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void clearQueryPoints();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } splitAxis;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } node;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } edge;
    } treeVisual;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::vec2 position;
            float radius;
            int numPoints;
            int treeSize;
        } constants{};

        struct {
            uint64_t start;
            uint64_t end;
        } runtime;
    } searchCompute;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        bool on{};
    } search;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanBuffer pointBuffer;
    VulkanBuffer treeBuffer;
    VulkanBuffer searchResultBuffer;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    Point* points;
    int numPoints{(1 << 15) - 1};
    int numSplits{0};
    Action* addSplit;
    Action* removeSplit;
    Action* clearSplits;
    Action* treeToggle;
    SearchArea searchArea;
    Point searchPoint;
    std::vector<Point*> searchResults;
    std::vector<Point*> missedPoints;
    std::vector<int> tree;
    VkQueryPool queryPool;
    int numSearchPoints = 200;
    bool useGPU{};
    bool debugSearch{};
    bool visualizeTree{};
    bool executeSearch{};
};