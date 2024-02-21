#include "VulkanBaseApp.h"
#include "domain.hpp"

class SpacePartitioning : public VulkanBaseApp{
public:
    explicit SpacePartitioning(const Settings& settings = {});

protected:
    void initApp() override;

    void createPoints();

    void generatePoints();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderPoints(VkCommandBuffer commandBuffer);

    void renderSpitAxis(VkCommandBuffer commandBuffer);

    void renderSearchArea(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

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
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
    Point* points;
    int numPoints{(1 << 15) - 1};
    int numSplits{0};
    Action* addSplit;
    Action* removeSplit;
    Action* clearSplits;
    SearchArea searchArea;
    Point searchPoint;
    std::vector<Point*> searchResults;
    std::vector<Point*> missedPoints;
    std::vector<int> tree;
    bool debugSearch{};
};