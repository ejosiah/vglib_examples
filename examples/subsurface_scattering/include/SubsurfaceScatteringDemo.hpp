#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "ComputePipelins.hpp"
#include "Sampler.hpp"
#include "taa/Taa.hpp"

class SubsurfaceScatteringDemo : public VulkanBaseApp{
public:
    explicit SubsurfaceScatteringDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCompute();

    void initTaa();

    void initShadowMap();

    void initCamera();

    void initGBuffers();

    void initUniforms();

    void loadModel();

    void createSkybox();

    void loadEnvironment();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderModel(VkCommandBuffer commandBuffer);

    void renderEnvironment(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer);

    void captureShadow(VkCommandBuffer commandBuffer);

    void finalLighting(VkCommandBuffer commandBuffer);

    void toneMapp(VkCommandBuffer commandBuffer);

    void sssBlur(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void newFrame() override;

protected:
    struct {
        Pipeline lightingPass1;
        Pipeline lightingFinal;
        Pipeline environment;
        Pipeline tone_mapping;
        Pipeline shadowMap;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    VulkanDrawable model;

    struct {
        Texture diffuse;
        Texture specular;
        Texture color;
        Texture depth;
        Texture sssOutput;
    } gbuffer;

    Offscreen::RenderInfo lightingRenderInfo{};
    Offscreen::RenderInfo renderInfo{};

    struct UniformData {
        glm::mat4 lightSpaceMatrix{1};
        glm::mat3 envRotation{1};
        glm::vec2 pixelSize{1};
        float sssWidth{0.01};
        float specularRoughness{0.95};
        float specularIntensity{1.88};
        float bumpiness{0.9};
        float ambientFactor{1};
        float translucency{0.996};
        float near{1};
        float far{50};
        float lightNearPlane{0.01};
        float lightFarPlane{100};
        uint diffuse_tex_id{~0u};
        uint specular_tex_id{~0u};
        uint color_tex_id{~0u};
        uint depth_tex_id{~0u};
        uint sss_tex_id{~0u};
        uint sss_image_id{~0u};
        uint sss_enabled{0};
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;

    struct {
        VulkanBuffer gpu;
        gltf::Light* cpu{};
    } light;

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VkDescriptorSet uniformDescriptorSet{};

    struct {
        bool ssEnabled{true};
        bool taaEnabled{true};
        float scatteringRadius{14};
        float lightAngle{284};
        float envRotation{0};
        float sRoughness{0.95};
        float sIntensity{1.88};
        float bumpiness{0.9};
        float ambientFactor{1};
        float translucency{0.996};
    } options;

    struct {
        Texture albedo;
        Texture specular;
        Texture irradiance;
        Texture brdfLut;
        std::string path{"studio_small_09_2k.hdr"};
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
    } environment;

    struct {
        Texture texture;
        Offscreen::RenderInfo renderInfo;
        const VkFormat format{VK_FORMAT_D16_UNORM};
        const uint size{2048};
        const float depthBiasConstant{1.25f};
        const float depthBiasSlope{1.75f};
        glm::mat4 lightViewMatrix{1};
    } shadowMap;

    struct {
        VulkanBuffer vertices;
        VulkanBuffer indices;
    } skyBox;
    Offscreen offscreen{};
    ComputePipelines compute;
    Jitter jitter{};
    glm::vec2 jitterValue{};
    std::unique_ptr<taa::Taa> taa;
};