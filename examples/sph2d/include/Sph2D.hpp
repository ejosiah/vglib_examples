#include "VulkanBaseApp.h"
#include "FixedUpdate.hpp"
#include "Canvas.hpp"
#include <PrefixSum.hpp>

struct Domain{
    glm::vec2 lower{};
    glm::vec2 upper{};
};

struct GlobalData {
    glm::mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float mass;
    float radius;
    float viscousConstant;
    float gasConstant;
    int numParticles;
    int generator;
    float smoothingRadius;
    float restDensity;
    float h;
    float h2;
    float _2h3;
    float kernelMultiplier;
    float gradientMultiplier;
    int hashMapSize;
    int numCollisions;
    float time;
};

class Sph2D : public VulkanBaseApp{
public:
    explicit Sph2D(const Settings& settings = {});

protected:
    void initApp() override;

    void initGrid();

    void initPrefixSum();

    void createDescriptorPool();

    void initializeParticles();

    void generatePoints();

    void initSdf();

    static void setConstants(GlobalData& data);

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void runSph(VkCommandBuffer commandBuffer);

    void updateHashGrid(VkCommandBuffer commandBuffer);

    void computeHashMap(VkCommandBuffer commandBuffer);

    void computeHistogram(VkCommandBuffer commandBuffer);

    void computePartialSum(VkCommandBuffer commandBuffer);

    void reorder(VkCommandBuffer commandBuffer);

    void renderParticles(VkCommandBuffer commandBuffer);

    void collisionCheck(VkCommandBuffer commandBuffer);

    void computeDensity(VkCommandBuffer commandBuffer);

    void computeForces(VkCommandBuffer commandBuffer);

    void integrate(VkCommandBuffer commandBuffer);

    void prepareForCompute(VkCommandBuffer commandBuffer);

    void addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addShaderWriteToTransferReadBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void addTransferWriteToShaderReadBarrierBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers);

    void prepareForRender(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    void endFrame() override;

protected:
    struct {
        Pipeline particles;
    } render;

    struct {
        Pipeline generatePoints;
        Pipeline sdf;
        Pipeline density;
        Pipeline force;
        Pipeline collision;
        Pipeline integrate;
        Pipeline hashMap;
        Pipeline histogram;
        Pipeline reorder;
    } compute;

    struct {
        const int maxParticles{200000};
        VulkanBuffer position;
        VulkanBuffer velocity;
        VulkanBuffer forces;
        VulkanBuffer positionReordered;
        VulkanBuffer velocityReordered;
        std::array<VulkanBuffer, 2> density;
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
        VulkanDescriptorSetLayout densitySetLayout;
        std::array<VkDescriptorSet, 2> densitySet;
    } particles;

    struct {
        VulkanBuffer counts;
        VulkanBuffer hashes;
    } spatialHash;


    VulkanDescriptorSetLayout globalSetLayout;
    VkDescriptorSet globalSet;

    struct {
        VulkanBuffer gpu;
        GlobalData* cpu{};
    } globals;

    struct {
        float h{};
        float g{};
        float k{};
        bool start{false};
        bool reset{false};
        bool accelerate{true};
        bool fixedUpdate{true};
    } options;

    struct {
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet descriptorSet;
        Texture texture;
    } sdf;
    Texture texture;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;

    FixedUpdate fixedUpdate{240};
    static constexpr uint32_t workGroupSize = 256;

    struct {
        Canvas canvas;
        struct {
            Domain domain;
            float spacing{};
        } constants;
        bool show{false};
    } grid;

    PrefixSum prefixSum;
};