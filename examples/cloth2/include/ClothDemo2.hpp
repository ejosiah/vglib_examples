#include "VulkanBaseApp.h"
#include "Cloth.hpp"
#include "Integrator.hpp"
#include "Geometry.hpp"

class ClothDemo2 : public VulkanBaseApp {
public:
    explicit ClothDemo2(const Settings& settings = {});

protected:
    void initApp() override;

    void initIntegrators();

    void createFloor();

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
    } shading = Shading::WIREFRAME;

    bool showPoints{};
    bool showNormals{};
    bool simRunning{};
    std::unique_ptr<Integrator> integrator;
};