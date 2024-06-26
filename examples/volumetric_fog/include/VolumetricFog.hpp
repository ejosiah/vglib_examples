#include "Scene.hpp"
#include "Sky.hpp"
#include "ShadowMap.hpp"
#include "VulkanBaseApp.h"
#include "AsyncModelLoader.hpp"
#include "Fog.hpp"

struct Pipeline{
    VulkanPipelineLayout layout;
    VulkanPipeline pipeline;
};

class VolumetricFog : public VulkanBaseApp{
public:
    explicit VolumetricFog(const Settings& settings = {});

protected:
    void initApp() override;

    void createHaltonSamples();

    void loadBlueNoise();

    void initBindlessDescriptor();

    void bakeVolumetricNoise();

    void initLoader();

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

    void updateMeshDescriptorSet();

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

    void renderVolumeOutline(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void updateSunDirection();

    void castShadow();

    void computeFog();

    void newFrame() override;

    void endFrame() override;

    void createBox();

    void beforeDeviceCreation() override;

protected:
    Pipeline render;
    Pipeline rayMarch;
    Pipeline dataInjection;
    Pipeline spatialFilter;
    Pipeline temporalFilter;
    Pipeline lightIntegration;
    Pipeline lightContrib;
    Pipeline volumeOutline;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
//    std::unique_ptr<OrbitingCameraController> camera;
    std::unique_ptr<FirstPersonCameraController> camera;
    Sky sky;
    std::shared_ptr<asyncml::Model> m_sponza;
    struct {
        glm::vec3 direction{1, 0, 0};
        float zenith{45};
        float azimuth{0};
    } sun;

    bool raymarch = false;
    Fog m_fog;
    Scene m_scene;
    uint32_t zSamples = 128;
    ShadowMap m_shadowMap;
    BindlessDescriptor m_bindLessDescriptor;
    std::unique_ptr<asyncml::Loader> m_loader;
    VulkanDescriptorSetLayout m_meshSetLayout;
    VkDescriptorSet m_meshDescriptorSet;
    Texture dummyTexture;
    Texture blueNoise;
    Texture m_volumeNoise;
    std::vector<glm::vec2> m_haltonSamples;
    int32_t m_jitterIndex{};
    static constexpr int32_t JitterPeriod = 8;
    uint32_t fogDataImageId = ~0u;
    std::array<uint32_t, 2> lightContribImageId{~0u, ~0u};
    uint32_t lightScatteringImageId = ~0u;
    uint32_t volumeNoiseImageId = ~0u;
    std::shared_ptr<asyncml::Volume> m_smoke;
    VulkanBuffer boxBuffer;
};