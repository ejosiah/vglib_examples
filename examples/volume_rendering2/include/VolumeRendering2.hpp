#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Volume.hpp"

struct VolumeInfo {
    glm::mat4 worldToVoxelTransform;
    glm::mat4 voxelToWordTransform;
    glm::vec3 bmin{MAX_FLOAT};
    glm::vec3 bmax{MIN_FLOAT};
};

struct SceneData {
    glm::vec3 lightDirection{1};
    glm::vec3 lightColor{1};
    glm::vec3 scattering{0.1};
    glm::vec3 absorption{100};
    glm::vec3 extinction;
    glm::vec3 cameraPosition;
    float primaryStepSize{1};
    float shadowStepSize{1};
    float gain{0.2};
    float cutoff{0.005};
    float isoLevel{0};
    int shadow{0};
    float lightConeSpread{0.1};
};

struct Scene {
    VulkanBuffer gpu;
    SceneData* cpu;
};

class VolumeRendering2 : public VulkanBaseApp{
public:
    explicit VolumeRendering2(const Settings& settings = {});

protected:
    void initApp() override;

    void initScene();

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

    void renderFogVolume(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } level_set;
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } fog;
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
    Scene scene{};
    std::vector<glm::vec3> box {
            {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
            {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1},
    };
};