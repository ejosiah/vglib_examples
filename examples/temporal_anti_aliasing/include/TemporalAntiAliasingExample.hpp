#include "VulkanBaseApp.h"
#include "AsyncModelLoader.hpp"
#include "Offscreen.hpp"
#include "Canvas.hpp"
#include "gltf/GltfLoader.hpp"

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
};

struct TaaData {
    uint32_t history_color_texture_index{};
    uint32_t taa_output_texture_index{};
    uint32_t velocity_texture_index{};
    uint32_t current_color_texture_index{};
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

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void initGpuData();

    void initLoader();

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

    void createComputePipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void offscreenRender(VkCommandBuffer commandBuffer);

    void renderGround(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void updateScene(float time);

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void endFrame() override;

protected:
    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } model;

        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } ground;
    } _render;

    struct {
        struct {
            VulkanPipelineLayout layout;
            VulkanPipeline pipeline;
        } velocity;
    } _compute;

    VulkanDescriptorPool _descriptorPool;
    VulkanCommandPool _commandPool;
    std::vector<VkCommandBuffer> _commandBuffers;
    VulkanPipelineCache _pipelineCache;
    std::unique_ptr<FirstPersonCameraController> _camera;
//    std::unique_ptr<OrbitingCameraController> _camera;

    Scene _scene;
    TaaConstants _taa;
    BindlessDescriptor _bindlessDescriptor;
    VulkanBuffer _offScreenQuad;
    struct {
        Texture color;
        Texture depth;
        Texture velocity;
        std::array<Texture, 2> history;
        uint32_t width{};
        uint32_t height{};
    } _textures;

    Offscreen::RenderInfo _offscreenInfo{};
    Canvas _canvas;
    VkDescriptorSet _displaySet{};
    std::shared_ptr<gltf::Model> _model;
    std::unique_ptr<gltf::Loader> _loader;
};