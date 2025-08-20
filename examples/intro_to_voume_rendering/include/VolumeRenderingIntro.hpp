#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Offscreen.hpp"
#include "Sampler.hpp"
#include "taa/Taa.hpp"
#include "ComputePipelins.hpp"
#include <latch>

struct Grid {
    Texture density;
    Texture emission;
    glm::mat4 worldToLocal;
    glm::mat4 localToWorld;
    struct {
        glm::vec3 min{MAX_FLOAT};
        glm::vec3 max{MIN_FLOAT};
    } bounds;
    uint32_t binding_id{~0u};
    uint32_t emission_binding_id{~0u};
    float maxDensity{1};
    float maxEmission{0};
};

class VolumeRenderingIntro : public VulkanBaseApp{
public:
    explicit VolumeRenderingIntro(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initTaa();

    void loadVolume();

    void loadPrimitives();

    void loadBlueNoise();

    void initOffscreen();

    void initUniforms();

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

    void renderUI(VkCommandBuffer commandBuffer);

    void renderOffscreen(VkCommandBuffer commandBuffer);

    void renderVolume(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void endFrame() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;

        Pipeline procedural;
        Pipeline grid;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    Offscreen::RenderInfo renderInfo;
    Offscreen offscreen;
    struct {
        Texture color;
        Texture depth;
    } gbuffer;

    struct UniformData {
        glm::mat4 projection{1};
        glm::mat4 view{1};
        glm::mat4 worldToTextureSpace{1};
        glm::vec4 bmin{-1.1};
        glm::vec4 bmax{1.1};
        glm::vec4 scatter{1, 1, 1, 10};
        glm::vec4 absorption{1, 1, 1, 5};
        glm::vec2 resolution;
        float near{1};
        float far{1};
        int density_method{3};
        float frequency{8};
        float falloff{0.2};
        float bias{0.2};
        float max_density{1};
        float max_emission{0};
        float emission_zero{0};
        uint frame{0};
        uint color_tex_id{0u};
        uint depth_tex_id{~0u};
        uint blue_noise_tex_id{~0u};
        uint volume_tex_id{~0u};
        uint volume_emission_tex_id{~0u};
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } uniforms;
    struct {
        struct {
            VulkanBuffer vertices;
            VulkanBuffer indexes;
        } sphere;
    } primitives;
    const VkFormat colorFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VkDescriptorSet uniformDescriptorSet{};
    Jitter jitter{};
    glm::vec2 jitterValue{};
    std::unique_ptr<taa::Taa> taa;
    Texture blueNoise;
    Grid volume;
    float scale{50};
    bool taaEnabled{true};
};