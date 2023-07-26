#include "VulkanBaseApp.h"

constexpr uint32_t TRANSMITTANCE_TEXTURE_WIDTH = 256;
constexpr uint32_t TRANSMITTANCE_TEXTURE_HEIGHT = 64;
constexpr uint32_t SCATTERING_TEXTURE_WIDTH = 256;
constexpr uint32_t SCATTERING_TEXTURE_HEIGHT = 128;
constexpr uint32_t SCATTERING_TEXTURE_DEPTH = 32;
constexpr uint32_t IRRADIANCE_TEXTURE_WIDTH = 64;
constexpr uint32_t IRRADIANCE_TEXTURE_HEIGHT = 16;

constexpr float kSunAngularRadius = 0.00935 / 2;
constexpr float kSunSolidAngle = glm::pi<float>() * kSunAngularRadius * kSunAngularRadius;
constexpr float kLengthUnitInMeters = 1000;

class AtmosphericScattering : public VulkanBaseApp{
public:
    explicit AtmosphericScattering(const Settings& settings = {});

protected:
    void initApp() override;

    void loadAtmosphereLUT();

    void initBuffers();

    void initUbo();

    void initCamera();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderAtmosphere(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void setView(float viewDistanceMeters, float viewZenithAngleRadians, float viewAzimuthAngleRadians,
                 float sunZenithAngleRadians, float sunAzimuthAngleRadians, float exposure);

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        Texture irradiance;
        Texture transmittance;
        Texture scattering;
    } atmosphereLUT;

    struct Ubo {
        glm::mat4 model_from_view{1};
        glm::mat4 view_from_clip{1};
        alignas(16) glm::vec3 camera{0};
        alignas(16) glm::vec3 white_point{1};
        alignas(16) glm::vec3 earth_center;
        alignas(16) glm::vec3 sun_direction;
        alignas(16) glm::vec3 sun_size{0};
        alignas(16) glm::vec3 sphereAlbedo;
        alignas(16) glm::vec3 groundAlbedo;
        float exposure;
    };

    float viewDistanceMeters{9000};
    float viewZenithAngleRadians{1.47};
    float viewAzimuthAngleRadians{-0.1};
    float sunZenithAngleRadians{1.3};
    float sunAzimuthAngleRadians{2.9};
    float exposure{10};
    int view{0};
    int numViews{9};

    Ubo* ubo{};
    VulkanBuffer uboBuffer;
    VulkanDescriptorSetLayout uboSetLayout;
    VkDescriptorSet uboSet;

    VulkanBuffer screenBuffer;

    VulkanDescriptorSetLayout atmosphereLutSetLayout;
    VkDescriptorSet atmosphereLutSet;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
};