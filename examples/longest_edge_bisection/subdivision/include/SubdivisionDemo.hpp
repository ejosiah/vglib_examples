#include "VulkanBaseApp.h"
#include "cbt/cbt.hpp"

class SubdivisionDemo : public VulkanBaseApp{
public:
    explicit SubdivisionDemo(const Settings& settings = {});

    static void splitCallback(cbt::Tree&, const cbt::Node& node, const void* userData);

    static void mergeCallback(cbt::Tree&, const cbt::Node& node, const void* userData);

    bool isInside(const float faceVertices[][3]) const;

protected:

    struct CbtInfo {
        uint maxDepth;
        uint nodeCount;
    };

    enum class Mode : int { Triangle = 0, Square };
    enum class Backend : int {GPU = 0, CPU};

    void initApp() override;

    void initCamera();

    void initCBT();

    void createBuffers();

    void beforeDeviceCreation() override;

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

    void renderUI(VkCommandBuffer commandBuffer);

    void renderTarget(VkCommandBuffer commandBuffer);

    void renderTriangle(VkCommandBuffer commandBuffer);

    void transferCBT(VkCommandBuffer commandBuffer);

    void transferCBTToHost(VkCommandBuffer commandBuffer);

    void lebDispatcher(VkCommandBuffer commandBuffer);

    void computeToDrawBarrier(VkCommandBuffer commandBuffer);

    void hostToGpuTransferBarrier(VkCommandBuffer commandBuffer);

    void gpuToHostTransferBarrier(VkCommandBuffer commandBuffer);

    void computeToComputeBarrier(VkCommandBuffer commandBuffer);

    void updateSubdivision();

    void cbtDispatch(VkCommandBuffer commandBuffer);

    void lebSubdivision(VkCommandBuffer commandBuffer, int pingPong);

    void getCbtInfo(VkCommandBuffer commandBuffer);

    void sumReducePrePass(VkCommandBuffer commandBuffer);

    void sumReduceCbt(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    static constexpr int64_t CBT_MAX_DEPTH = 27;
    static constexpr int64_t CBT_INIT_MAX_DEPTH = 1;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } target;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } triangle;
    } render;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } lebDispatcher;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } cbtDispatcher;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } cbtInfo;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            uint pass{0};
        } sumReducePrepass;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            uint pass{0};
        } sumReduce;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
            struct {
                glm::vec2 target;
                int updateFlag;
            } constants{};
        } subdivision;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    Mode mode{Mode::Square};
    Backend backend{Backend::GPU};
    glm::vec2 target{0.49951f, 0.41204f};

    float maxDepth{CBT_MAX_DEPTH};
    int initMaxDepth{CBT_INIT_MAX_DEPTH};
    VulkanBuffer dummyBuffer;

    struct {
        VulkanBuffer gpu;
        cbt::Tree cbt;
        CbtInfo cbt_info;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet;
    } leb;
    VulkanBuffer transferBuffer;
    VulkanBuffer drawBuffer;
    VulkanBuffer dispatchBuffer;
    VulkanBuffer cbtInfoBuffer;
    void* transferLink{};

};