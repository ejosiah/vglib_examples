#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "GroundPatch.hpp"

class PlanetDemo : public VulkanBaseApp{
public:
    explicit PlanetDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createGroundPlane();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    void newFrame() override;

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

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    GroundPatch ground_patch_;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } ground;
};