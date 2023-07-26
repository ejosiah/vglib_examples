#include "VulkanBaseApp.h"
#include "scene.hpp"
#include "terrain.hpp"
#include "sky_dome.hpp"
#include "atmosphere.hpp"
#include "shadow_volume_generator.hpp"
#include "clouds.hpp"

class OpenWorldDemo : public VulkanBaseApp{
public:
    explicit OpenWorldDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void createSceneGBuffer();

    void createSamplers();

    void loadAtmosphereLUT();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void updateScene(float time);

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;
    SceneData sceneData;
    std::unique_ptr<Terrain> terrain;
    std::unique_ptr<SkyDome> skyDome;
    std::unique_ptr<Atmosphere> atmosphere;
    std::unique_ptr<ShadowVolumeGenerator> shadowVolumeGenerator;
    std::unique_ptr<Clouds> clouds;
    glm::vec3 gravity{0, -9.8, 0};

    std::shared_ptr<SceneGBuffer> sceneGBuffer;
    std::shared_ptr<Samplers> samplers;

    std::shared_ptr<AtmosphereLookupTable> atmosphereLUT;
};