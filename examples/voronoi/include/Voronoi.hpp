#include "VulkanBaseApp.h"
#include <PrefixSum.hpp>
#include <CDT.h>
#include "Profiler.hpp"

struct Circle {
    glm::vec2 center;
    float radius;
};


namespace std {
    template<>
    struct hash<glm::vec3>{

        std::size_t operator()(const glm::vec3& c) const {
            glm::uvec3 id(glm::floatBitsToUint(c.x), glm::floatBitsToUint(c.y), glm::floatBitsToUint(c.z));
            return (id.x * 541) ^ (id.y * 79) ^ (id.z * 31);
        }
    };
}

class Voronoi : public VulkanBaseApp{
public:
    explicit Voronoi(const Settings& settings = {});

protected:
    void initApp() override;

    void initProfiler();

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

    void renderVoronoiDiagram(VkCommandBuffer commandBuffer);

    void renderDelaunayTriangles(VkCommandBuffer commandBuffer);

    void renderGenerators(VkCommandBuffer commandBuffer);

    void renderCircumCircles(VkCommandBuffer commandBuffer);

    void renderCentroids(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void convergeToCentroid(VkCommandBuffer commandBuffer);

    void computeRegionAreas(VkCommandBuffer commandBuffer);

    void computeHistogram(VkCommandBuffer commandBuffer);

    void computePartialSum(VkCommandBuffer commandBuffer);

    void reorderRegions(VkCommandBuffer commandBuffer);

    void generateVoronoiRegions(VkCommandBuffer commandBuffer);

    void regionalReductionMap(VkCommandBuffer commandBuffer);

    void regionalReductionReduce(VkCommandBuffer commandBuffer);

    void convergeToCentroidRegionalReduction(VkCommandBuffer commandBuffer);

    void generateVoronoiRegions2(VkCommandBuffer commandBuffer);

    void addCentroidReadBarrier(VkCommandBuffer commandBuffer);

    void addCentroidWriteBarrier(VkCommandBuffer commandBuffer);

    void addTransferSrcBarrier(VkCommandBuffer commandBuffer, std::span<VkImage> images);

    void addTransferDstBarrier(VkCommandBuffer commandBuffer, std::span<VkImage> images);

    void addTransferSrcToShaderReadBarrier(VkCommandBuffer commandBuffer, std::span<VkImage> images);

    void addTransferDstToShaderReadBarrier(VkCommandBuffer commandBuffer, std::span<VkImage> images);

    void addVoronoiImageWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageReadToWriteBarrier(VkCommandBuffer commandBuffer) const;

    void addVoronoiImageWriteToReadBarrier(VkCommandBuffer commandBuffer) const;

    void addRegionalReductionImageWriteToReadBarrier(VkCommandBuffer commandBuffer) const;

    void copyGeneratorPointsToCpu(VkCommandBuffer commandBuffer);

    void updateTriangles();

    void triangulate(std::span<CDT::V2d<float>> points);

    Circle calculateCircumCircle(std::span<CDT::V2d<float>> points, const CDT::Triangle& triangle);

    void update(float time) override;

    void checkAppInputs() override;

    void newFrame() override;

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
    } rendervoronoi;

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

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } outline;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } center;
        bool showOutline{};
        bool showCenter{};
        Action* outlineToggle{};
        Action* centerToggle{};
    } circumCircle;

    VulkanBuffer colors;

    struct {
        int renderCentroid{0};
        float threshold{0};
        float convergenceRate{1};
        int screenWidth{0};
        int screenHeight{0};
        int numGenerators{0};
    } constants;

    struct {
        Texture color;
        Texture depth;
    } gBuffer;

    Texture vdSwapTexture;
    VulkanDescriptorSetLayout vdSetLayout;
    std::array<VkDescriptorSet, 2> vdSet;

    struct DelaunayTriangles {
        VulkanBuffer vertices;
        VulkanBuffer triangles;
        uint32_t numTriangles;
        std::vector<Circle> circumCircles;
        struct  {
            std::vector<CDT::V2d<float>> vertices;
            std::vector<CDT::Triangle> triangles;
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
    int numGenerators{8000};
    std::unordered_map<glm::vec3, int> siteMap;

    VulkanDescriptorSetLayout voronoiRegionsSetLayout;
    VkDescriptorSet voronoiDescriptorSet;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    VulkanBuffer clipVertices;
    VulkanBuffer voronoiDiagramBuffer;

    std::vector<std::string> queryIds{
        "generate_voronoi_regions", "compute_region_area", "compute_histogram",
        "partial_sum", "reorder_regions", "converge_to_centroid"
    };

    struct {
        bool requested{};
        glm::vec2 position;
    } inspectRegion;

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

    bool useRegionalReduction{true};

    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    PrefixSum prefixSum;
    Profiler profiler;
};