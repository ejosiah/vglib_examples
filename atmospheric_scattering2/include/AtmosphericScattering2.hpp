#include "VulkanBaseApp.h"
#include "atmosphere.hpp"
class AtmosphericScattering2 : public VulkanBaseApp{
public:
    explicit AtmosphericScattering2(const Settings& settings = {});

protected:
    void initApp() override;

    void initUBO();

    void createTextures();

    void createCameraVolume();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void createComputePipelines();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void updateSunDirection();

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } preview;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } cameraVolume;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;
    std::unique_ptr<Atmosphere> atmosphere;

    struct {
        Texture transmittance;
        Texture inScattering;
        glm::ivec3 size{32};
    } atmosphereVolume;


    struct Ubo {
        glm::mat4 inverse_projection{1};
        glm::mat4 inverse_view{1};
        alignas(16) glm::vec3 camera{0};
        alignas(16) glm::vec3 white_point{1};
        alignas(16) glm::vec3 earth_center;
        alignas(16) glm::vec3 sun_direction;
        alignas(16) glm::vec3 sun_size{0};
        alignas(16) glm::vec3 sphereAlbedo;
        alignas(16) glm::vec3 groundAlbedo;
        float exposure;
        float near;
        float far;
        int frame;
    };

    struct {
        float zenith{45};
        float azimuth{0};
    } sun;

    Ubo* ubo{};
    VulkanBuffer uboBuffer;
    VulkanDescriptorSetLayout uboSetLayout;
    VkDescriptorSet uboSet;

    VulkanDescriptorSetLayout cameraVolumeSetLayout;
    VkDescriptorSet cameraVolumeSet;

    VulkanDescriptorSetLayout atmosphereVolumeSetLayout;
    VkDescriptorSet atmosphereVolumeSet;

    VulkanBuffer screenBuffer;
    float exposure{10};

    static constexpr float Z_NEAR = 1 * meter;
    static constexpr float Z_FAR = 100 * km;
};