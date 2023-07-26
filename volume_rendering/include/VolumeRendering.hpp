#include "VulkanBaseApp.h"



class VolumeRendering : public VulkanBaseApp{
public:
    explicit VolumeRendering(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initRenderingData();

    void loadVolumeData();

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

    void renderVolume(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::mat4 mvp{1};
            glm::vec3 viewDir{1};
            int numSlices{512};
        } constants;

    } sliceRenderer;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::mat4 mvp{1};
            alignas(16) glm::vec3 camPos{1};
            alignas(16) glm::vec3 stepSize{1};
        } constants;

    } rayMarchRenderer;

    enum class VolumeRenderMethod : int {
        Slice = 0, RayMarch
    };

    VolumeRenderMethod volumeRenderMethod = VolumeRenderMethod::RayMarch;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    VulkanBuffer vertexBuffer;
    VulkanBuffer cubeBuffer;
    VulkanBuffer cubeIndexBuffer;
    VulkanDescriptorSetLayout volumeDescriptorSetLayout;
    VkDescriptorSet volumeSet;
    Texture volumeTexture;
};