#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "ResourcePool.hpp"
#include "Models.hpp"

class TerrainMC : public VulkanBaseApp{
public:
    explicit TerrainMC(const Settings& settings = {});

protected:
    void initApp() override;

    void checkInvariants();

    void initCamera();

    void initBindlessDescriptor();

    void createCube();

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

    void renderCamera(VkCommandBuffer commandBuffer);

    void renderCube(VkCommandBuffer commandBuffer, Camera& acamera, const glm::mat4& model, bool outline = false);

    void newFrame() override;

    void update(float time) override;

    void checkAppInputs() override;

    void updateVisibilityList();

    void cleanup() override;

    void onPause() override;

    void computeCameraBounds();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } camRender;

    struct {
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } solid;
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } outline;
        std::vector<glm::mat4> instances;
    } cube;

    struct {
        VulkanBuffer vertices;
        glm::mat4 transform{1};
        glm::mat4 aabb{1};
    } cameraBounds;

    std::vector<glm::mat4> visibleList;
    std::vector<glm::mat4> skipList;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<SpectatorCameraController> camera;
    Camera debugCamera{};
    BindlessDescriptor bindlessDescriptor;
    ResourcePool<glm::mat4> cubes;
    ResourcePool<std::tuple<glm::vec3, glm::vec3>> boundingBoxes;
    Vertices cBounds;

    glm::mat4 tinyCube;
    CameraInfo cameraInfo{};
    std::vector<VulkanBuffer> cameraInfoGpu;
    std::array<glm::vec4, 8> corners{};
};