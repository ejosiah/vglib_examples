#include "VulkanBaseApp.h"
#include "Cloth.hpp"
#include "Integrator.hpp"
#include "Geometry.hpp"
#include "VulkanRayTraceModel.hpp"
#include "VulkanRayQuerySupport.hpp"

class ClothDemo2 : public VulkanBaseApp, public VulkanRayQuerySupport {
public:
    explicit ClothDemo2(const Settings& settings = {});

    ~ClothDemo2() override = default;

protected:
    void initApp() override;

    void initIntegrators();

    void createFloor();

    void beforeDeviceCreation() override;

    void loadModel();

    void createCloth();

    void initCamera();

    void createGeometry();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderFloor(VkCommandBuffer commandBuffer);

    void renderCloth(VkCommandBuffer commandBuffer);

    void renderGeometry(VkCommandBuffer commandBuffer);

    void renderModel(VkCommandBuffer commandBuffer);

    void renderPoints(VkCommandBuffer commandBuffer);

    void renderNormals(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        Pipeline wireframe;
        Pipeline points;
        Pipeline normals;
        Pipeline floor;
        Pipeline solid;
        Pipeline solidTex;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::shared_ptr<Cloth> cloth;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
        uint32_t indexCount;
    } floor;

    std::shared_ptr<Geometry> geometry;

    enum class Shading : int {
        WIREFRAME = 0,
        SHADED = 1
    } shading = Shading::SHADED;

    bool showPoints{};
    bool showNormals{};
    bool simRunning{};
    std::unique_ptr<Integrator> integrator;
    VulkanDrawable model;

    VulkanDescriptorSetLayout accStructDescriptorSetLayout;
    VulkanDescriptorSetLayout materialSetLayout;
    VkDescriptorSet accStructDescriptorSet;
    std::vector<rt::Instance> asInstances;
    rt::AccelerationStructureBuilder accStructBuilder;
    VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR posFetchFeature{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR
    };
    int clothColor = 0;
    int materialId = 0;

    enum class Collider : int {
        Sphere = 0, Box, Cow, None
    } collider = Collider::Sphere;

    struct {
        glm::vec3 position{0};
        glm::vec3 scale{1};
        glm::vec3 rotation{0};
    } xform{};
};