#include "VulkanBaseApp.h"
#include <PrefixSum.hpp>
#include <filesystem>
#include <optional>
#include "ImGuiPlugin.hpp"

struct GlobalData {
    glm::mat4 projection;
    int maxPoints;
    int numPoints;
    float convergenceRate{0.05};
    int width;
    int height;
};

struct Globals {
    VulkanBuffer gpu;
    GlobalData* cpu;
};

class WeightedVoronoiStippling : public VulkanBaseApp{
public:
    explicit WeightedVoronoiStippling(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initBuffers();

    void initPrefixSum();

    void initTexture(Texture& texture, uint32_t width, uint32_t height, VkFormat format
                     , VkImageAspectFlags  aspect = VK_IMAGE_ASPECT_COLOR_BIT
                     , VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

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

    void generateVoronoiRegions(VkCommandBuffer commandBuffer);

    void computeRegionAreas(VkCommandBuffer commandBuffer);

    void convergeToCentroid(VkCommandBuffer commandBuffer);

    void addCentroidReadBarrier(VkCommandBuffer commandBuffer);

    void addCentroidWriteBarrier(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void renderStipples(VkCommandBuffer commandBuffer);

    void generateStipple();

    void generatePoints();

    void computeHistogram(VkCommandBuffer commandBuffer);

    void computePartialSum(VkCommandBuffer commandBuffer);

    void reorderRegions(VkCommandBuffer commandBuffer);

    void initScratchData();

    void openFileDialog();

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void loadImage(const std::filesystem::path& path);

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } render;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } generatePoints;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } sum;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } computeCentroid;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            Texture depthBuffer;
        } voronoi;
        bool run{};
        Texture voronoiRegions;
        ImTextureID voronoiRegionsId{};
        Texture points;
        ImTextureID pointsId{};
    } stipple;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } histogram;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } reorder;

    struct {
        std::string name;
        Texture texture;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet;
        ImTextureID textureId{ nullptr };
    } source;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    VulkanBuffer points;
    VulkanBuffer centroids;
    VulkanBuffer regions;
    VulkanBuffer regionReordered;
    VulkanBuffer counts;
    Globals globals;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cone;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } debug;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::optional<std::filesystem::path> imagePath;
    PrefixSum prefixSum;
    static constexpr int MaxPoints = 100000;
};