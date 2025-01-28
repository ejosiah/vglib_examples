#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Volume.hpp"

struct CopyBufferToImage {
    VkBuffer srcBuffer{};
    VkBuffer eSrcBuffer{};
    VkImage dstImage{};
    VkImage eDstImage{};
    VkBufferImageCopy region{};
};

struct VolumeInfo {
    glm::mat4 worldToVoxelTransform;
    glm::mat4 voxelToWordTransform;
    glm::vec3 bmin{MAX_FLOAT};
    glm::vec3 bmax{MIN_FLOAT};
    glm::uvec3 dimensions;
};

struct VolumeFrame {
    VulkanBuffer density;
    VulkanBuffer emission;
};

struct VolumeMetadata {
    VulkanBuffer info;
    glm::vec3 bmin{MAX_FLOAT};
    glm::vec3 bmax{MIN_FLOAT};
    glm::uvec3 dimensions;
    VkDescriptorSet descriptorSet{};
};

using VolumeAnimation = Animation<VolumeFrame, VolumeMetadata>;

struct SceneData {
    glm::vec3 lightDirection{1};
    glm::vec3 lightColor{1};
    glm::vec3 scattering{50};
    glm::vec3 absorption{0.1};
    glm::vec3 extinction;
    glm::vec3 cameraPosition;
    float primaryStepSize{1};
    float shadowStepSize{1};
    float gain{0.2};
    float cutoff{0.005};
    float isoLevel{0};
    int shadow{0};
    float lightConeSpread{0.1};
    int currentFrame{0};
    int texturePoolSize{};
    int numSteps{200};
    float asymmetric_factor;
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

    void initTextureCopyData();

    void initCamera();

    void initBindlessDescriptor();

    void loadVolume();

    void loadAnimation();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void transferFramesToGpu(VkCommandBuffer commandBuffer);

    void updateAnimationDescriptorSets();

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

    VulkanDescriptorSetLayout volumeInfoSetLayout;
    VkDescriptorSet volumeInfoSet{};
    Scene scene{};
    std::vector<glm::vec3> box {
            {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
            {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1},
    };
    VolumeAnimation animation;
    static constexpr int poolSize = 1;
    static constexpr int aframeCount = 1;
    static constexpr int batchSize = 6;
    std::array<CopyBufferToImage, batchSize> regions{};
    struct {
        std::array<Texture, poolSize> density;
        std::array<Texture, poolSize> emission;
        int used{0};
        int freeHead{0};
    } pool;
    int pendingFrameOffset{};
    bool copyPending{};
    std::array<VkImageMemoryBarrier2, batchSize * 2> imageBarriersToTransfer{};
    std::array<VkImageMemoryBarrier2, batchSize * 2> imageBarriersToShaderRead{};
    VkDependencyInfo tDependencyInfo{};
    VkDependencyInfo sDependencyInfo{};

};