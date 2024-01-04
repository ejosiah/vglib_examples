#include "Scene.hpp"
#include "Sky.hpp"
#include "ShadowMap.hpp"
#include "VulkanBaseApp.h"
#include "Fog.hpp"

class VolumetricFog : public VulkanBaseApp{
public:
    explicit VolumetricFog(const Settings& settings = {});

protected:
    void initApp() override;

    void initSky();

    void initFog();

    void createFogTextures();

    void initShadowMap();

    void loadModel();

    void initScene();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateFogDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void renderWithVolumeTextures(VkCommandBuffer commandBuffer);

    void renderWithRayMarching(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void updateSunDirection();

    void castShadow();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } rayMarch;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
//    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<FirstPersonCameraController> camera;
    Sky sky;
    VulkanDrawable m_sponza;
    struct {
        glm::vec3 direction{1, 0, 0};
        float zenith{45};
        float azimuth{0};
    } sun;

    Fog m_fog;
    Scene m_scene;
    ShadowMap m_shadowMap;
};