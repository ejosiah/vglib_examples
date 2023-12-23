#include "VulkanBaseApp.h"

class CubeFractal : public VulkanBaseApp{
public:
    explicit CubeFractal(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createCube();

    void generateFractal(int maxDepth, std::vector<glm::mat4>& xforms);

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorSetLayout setLayout;
    VkDescriptorSet descriptorSet;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
//    std::unique_ptr<FirstPersonCameraController> camera;
    VulkanBuffer instanceXforms;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } cube;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } plane;

    uint32_t instanceCount = 1;
    static constexpr uint32_t MaxDepth = 5;
};