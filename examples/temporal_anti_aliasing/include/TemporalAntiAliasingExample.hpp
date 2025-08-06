#include "VulkanBaseApp.h"
#include "AsyncModelLoader.hpp"
#include "Offscreen.hpp"
#include "Canvas.hpp"
#include "gltf/GltfLoader.hpp"
#include "Sampler.hpp"
#include "taa/Taa.hpp"

struct SceneData {
    glm::mat4 current_view_projection{};
    glm::mat4 inverse_current_view_projection{};

    glm::mat4 previous_view_projection{};
    glm::mat4 inverse_previous_view_projection{};

    glm::mat4 world_to_camera{};
    glm::vec4 camera_position{};
    glm::vec3 camera_direction{};
    int current_frame{};

    glm::vec2 jitter_xy{};
    glm::vec2 previous_jitter_xy{};

    glm::vec2 resolution;
    int color_buffer_index;
    int depth_buffer_index;
};

struct TaaData {
    uint32_t history_color_texture_index{};
    uint32_t taa_output_texture_index{};
    uint32_t velocity_texture_index{};
    uint32_t current_color_texture_index{};
    int taaSimple{};
    int filter{};
    int sub_sample_filter{};
    int history_constraint{};
    int temporal_filtering{};
    int inverse_luminance_filtering{};
    int luminance_difference_filtering{};
};

struct Scene {
    VulkanBuffer gpu;
    SceneData* cpu{};
};

struct TaaConstants {
    VulkanBuffer gpu;
    TaaData* cpu{};
};

class TemporalAntiAliasingExample : public VulkanBaseApp{
public:
    explicit TemporalAntiAliasingExample(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initJitter();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void initLoader();

    void initTaa();

    void loadModel();

    void initScreenQuad();

    void initTextures();

    void initCanvas();

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void offscreenRender(VkCommandBuffer commandBuffer);

    void renderGround(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void applyTaa(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

    void newFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } model;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } placeHolder;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } ground;
    } _render;

    VulkanDescriptorPool _descriptorPool;
    VulkanCommandPool _commandPool;
    std::vector<VkCommandBuffer> _commandBuffers;
    VulkanPipelineCache _pipelineCache;
    std::unique_ptr<BaseCameraController> _camera;

    std::unique_ptr<taa::Taa> taa;
    glm::vec2 jitterValue{};
    taa::Settings taa_settings{};
    BindlessDescriptor _bindlessDescriptor;
    VulkanBuffer _offScreenQuad;
    struct {
        Texture color;
        Texture depth;
        uint32_t width{};
        uint32_t height{};
    } _textures;

    Jitter jitter{};

    struct {
        bool jitterEnabled{true};
        bool taaEnabled{true};
        bool simple{};
        int samplerType{static_cast<int>(SamplerType::Halton)};
        int jitterPeriod{8};
        int filter{1};
        int sub_sample_filter{1};
        int history_constraint{1};
        bool temporal_filtering{true};
        bool inverse_luminance_filtering{true};
        bool luminance_difference_filtering{true};
        const std::array<const char*, 4> samplers{ "Halton", "R2", "Hammersley", "IG" };
        const std::array<const char*, 2> filters{"Single", "Catmull Rom"};
        const std::array<const char*, 4> subSampleFilters{"None", "Mitchell", "Blackman Harris", "Catmull Rom"};
        const std::array<const char*, 5> historyConstraints{"None", "Clamp", "Clip", "Variance Clip", "Variance Clip with Clamp"};
        bool dirty{false};
    } options;

    Offscreen::RenderInfo _offscreenInfo{};
    Canvas _canvas;
    VkDescriptorSet _displaySet{};
    std::shared_ptr<gltf::Model> _model;
    std::unique_ptr<gltf::Loader> _loader;

    static constexpr uint32_t ColorBindingIndex = 3;
    static constexpr uint32_t DepthBindingIndex = 4;
};