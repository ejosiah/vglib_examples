#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Volume.hpp"

struct VolumeInfo {
    glm::mat4 worldToVoxelTransform;
    glm::mat4 voxelToWordTransform;
    glm::vec3 bmin;
    glm::vec3 bmax;
};

class VolumeRendering2 : public VulkanBaseApp{
public:
    explicit VolumeRendering2(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initBindlessDescriptor();

    void loadVolume();

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

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderLevelSet(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } level_set;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    VulkanBuffer volumeInfo;
    VolumeSet volume;
    Texture densityVolume;

    VulkanDescriptorSetLayout volumeDensitySetLayout;
    VulkanDescriptorSetLayout volumeInfoSetLayout;
    VkDescriptorSet volumeDensitySet{};
    VkDescriptorSet volumeInfoSet{};
};