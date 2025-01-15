#include "VulkanBaseApp.h"
#include "cbt/cbt.hpp"

class SubdivisionDemo : public VulkanBaseApp{
public:
    explicit SubdivisionDemo(const Settings& settings = {});

    static void splitCallback(cbt::Tree&, const cbt::Node& node, const void* userData);

    static void mergeCallback(cbt::Tree&, const cbt::Node& node, const void* userData);

    bool isInside(const float faceVertices[][3]) const;

protected:

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

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void renderTarget(VkCommandBuffer commandBuffer);

    void renderTriangle(VkCommandBuffer commandBuffer);

    void transferCBT(VkCommandBuffer commandBuffer);

    void updateSubdivision();

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    static constexpr int64_t CBT_MAX_DEPTH = 20;
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


    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    Mode mode{Mode::Triangle};
    Backend backend{Backend::CPU};
    glm::vec2 target{0.49951f, 0.41204f};

    float maxDepth{CBT_MAX_DEPTH};
    int initMaxDepth{CBT_INIT_MAX_DEPTH};
    VulkanBuffer dummyBuffer;

    struct {
        VulkanBuffer gpu;
        cbt::Tree cbt;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet;
    } leb;
    VulkanBuffer transferBuffer;

};