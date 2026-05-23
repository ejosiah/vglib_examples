#include "gltf/GltfLoader.hpp"
#include <VulkanBaseApp.h>
#include <Canvas.hpp>
#include <imgui.h>

class KtxViewer : public VulkanBaseApp{
public:
    explicit KtxViewer(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void loadTextures();

    void initCanvas();

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

    void textureViewerControls();

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
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Canvas canvas;
    Texture displacementMap;
    Texture normalMap;
    Texture moment0;
    Texture moment1;
    VulkanDescriptorSetLayout textureDescriptorSetLayout;
    VkDescriptorSet descriptorSet{};
    VkDescriptorSet normalMapDescriptorSet{};
    VkDescriptorSet slopeMoment0DescriptorSet{};
    VkDescriptorSet slopeMoment1DescriptorSet{};

    struct {
        int textureSlot{};
        std::map<const Texture*, ImTextureID> imguiTextureIds;
    } textureViewer;
};
