#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"

class PhysicalSimulation : public VulkanBaseApp{
public:
    explicit PhysicalSimulation(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initSimData();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderParticles(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void runSim(VkCommandBuffer commandBuffer);

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } integrate;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } update_attractors;
    } compute;

    struct Globals {
        float time{};
        float delta_time{};
        float decay_rate{0.0001};
        int num_attractors{1};
    };

    struct {
        VulkanBuffer velocity;
        VulkanBuffer position;
        VulkanBuffer attractors;
        struct {
            VulkanBuffer gpu;
            Globals* cpu{};
        } globals;
    } data;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet{};

    static constexpr uint32_t MAX_ATTRACTORS = 64;
    static constexpr uint32_t NUM_PARTICLES = 1 << 20;

};