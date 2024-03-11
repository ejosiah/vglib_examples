#include "VulkanBaseApp.h"
#include <PrefixSum.hpp>
#include <CDT.h>

class Voronoi : public VulkanBaseApp{
public:
    explicit Voronoi(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createDescriptorPool();

    void initPrefixSum();

    void createGBuffer();

    void initClipSpaceBuffer();

    void runGeneratePoints();

    void initGenerators();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderCones(VkCommandBuffer commandBuffer);

    void renderDelaunayTriangles(VkCommandBuffer commandBuffer);

    void renderGenerators(VkCommandBuffer commandBuffer);

    void renderCentroids(VkCommandBuffer commandBuffer);

    void convergeToCentroid(VkCommandBuffer commandBuffer);

    void computeRegionAreas(VkCommandBuffer commandBuffer);

    void computeHistogram(VkCommandBuffer commandBuffer);

    void computePartialSum(VkCommandBuffer commandBuffer);

    void reorderRegions(VkCommandBuffer commandBuffer);

    void generateVoronoiRegions(VkCommandBuffer commandBuffer);

    void generateVoronoiRegions2(VkCommandBuffer commandBuffer);

    void addCentroidReadBarrier(VkCommandBuffer commandBuffer);

    void addCentroidWriteBarrier(VkCommandBuffer commandBuffer);

    void addVoronoiImageWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageReadToWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageWriteToReadBarrier(VkCommandBuffer commandBuffer) const;

    void copyGeneratorPointsToCpu(VkCommandBuffer commandBuffer);

    void updateTriangles();

    void triangulate(std::span<CDT::V2d<float>> points);

    void update(float time) override;

    void checkAppInputs() override;

    void endFrame() override;

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
    } coneRender;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } voronoiCalc;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute_centroid;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } histogram;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } reorder;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } computeArea;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } seedVoronoiImage;
    struct{
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        int pass{0};
        int numPasses{0};
    } jumpFlood;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cone;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } triangle;

    VulkanBuffer colors;

    struct {
        int renderCentroid{0};
        float threshold{0};
        float convergenceRate{1};
        int screenWidth{0};
        int screenHeight{0};
    } constants;

    struct {
        Texture color;
        Texture depth;
    } gBuffer;

    struct DelaunayTriangles {
        VulkanBuffer vertices;
        VulkanBuffer triangles;
        uint32_t numTriangles;
        struct  {
            std::vector<CDT::V2d<float>> vertices;
            std::vector<uint32_t> indices;
        } cdt;
    } delaunayTriangles;


    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;

    VulkanBuffer generators;
    VulkanBuffer stagingBuffer;
    VulkanBuffer regions;
    VulkanBuffer regionReordered;
    VulkanBuffer centroids;
    VulkanBuffer counts;
    int numGenerators{200};

    VulkanDescriptorSetLayout voronoiRegionsSetLayout;
    VkDescriptorSet voronoiDescriptorSet;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    VulkanBuffer clipVertices;

    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    PrefixSum prefixSum;
};