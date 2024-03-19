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

    void initTexture(Texture& texture, uint32_t width, uint32_t height, VkFormat format
                     , VkImageAspectFlags  aspect = VK_IMAGE_ASPECT_COLOR_BIT
                     , VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createRegionalReducePipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void generateVoronoiRegions(VkCommandBuffer commandBuffer);

    void generateVoronoiRegions2(VkCommandBuffer commandBuffer);

    void addVoronoiImageWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageReadToWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageWriteToReadBarrier(VkCommandBuffer commandBuffer) const;

    void addCentroidReadBarrier(VkCommandBuffer commandBuffer);

    void addCentroidWriteBarrier(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void renderStipples(VkCommandBuffer commandBuffer);

    void generateStipple();

    void generatePoints();

    void regionalReductionMap(VkCommandBuffer commandBuffer);

    void regionalReductionReduce(VkCommandBuffer commandBuffer);

    void convergeToCentroidRegionalReduction(VkCommandBuffer commandBuffer);

    void addRegionalReductionImageWriteToReadBarrier(VkCommandBuffer commandBuffer);

    void addBufferMemoryBarriers(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

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
            Texture depthBuffer;
        } voronoi;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } seed;
        struct{
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            int pass{0};
            int numPasses{0};
        } jumpFlood;

        bool run{};
        Texture voronoiRegions;
        ImTextureID voronoiRegionsId{};
        Texture points;
        ImTextureID pointsId{};
    } stipple;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } map;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } reduce;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } centroid;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        glm::ivec2 dimensions;
        Texture texture;
    } regionalReduction;

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
    static constexpr int MaxPoints = 20000;
};