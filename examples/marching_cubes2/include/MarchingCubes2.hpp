#include "VulkanBaseApp.h"
#include "Floor.hpp"
#include "Voxel.hpp"
#include "Volume.hpp"
#include "Marcher.hpp"


class MarchingCubes2 : public VulkanBaseApp{
public:
    explicit MarchingCubes2(const Settings& settings = {});

protected:
    void initApp() override;

    void checkMeshShaderSupport();

    void beforeDeviceCreation() override;

    void initCamera();

    void loadVoxel();

    void initMarcher();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderMesh(VkCommandBuffer commandBuffer);

    void renderMeshWireframe(VkCommandBuffer commandBuffer);

    void rayMarch(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } rayMarch;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } render;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;

            struct {
                Camera camera;
                glm::vec3 bmin;
                float cubeSize;

                glm::vec3 bmax;
                float isoLevel{0};
            } constants;
        } cubeMarcher;
    } pipelines;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    Marcher cubeMarcher;
    Marcher::Mesh result;

    Voxels voxels;
    int shadingMode{1};
    float cubeSizeMultiplier{1};
    bool useMeshShader{};
    bool meshShaderSupported;
};