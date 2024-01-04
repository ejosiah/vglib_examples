#pragma once

#define MAX_IN_FLIGHT_FRAMES 1

#include "VulkanBaseApp.h"
#include "VulkanDescriptorSet.h"
#include <glm/glm.hpp>
#include <openvdb/openvdb.h>
#include <taskflow/taskflow.hpp>
#include "blur_image.hpp"
#include "Canvas.hpp"
#include "VdbAnimation.hpp"

enum class LoadState{
    READY, REQUESTED, LOADING, FAILED
};


struct VolumeData{
    std::string name;
    glm::vec3 boxMin;
    glm::vec3 boxMax;
    std::vector<float> data;
    float invMaxDensity;
};

struct CameraUbo{
    glm::mat4 projection;
    glm::mat4 view;
    glm::mat4 inverseProjection;
    glm::mat4 inverseView;
};

struct VolumeUbo{
    alignas(16) glm::vec3 boxMin;
    alignas(16) glm::vec3 boxMax;
    alignas(16) glm::vec3 lightPosition;
    alignas(16) glm::vec3 color;
    float invMaxDensity;
    float scatteringCoefficient;
    float absorptionCoefficient;
    float extinctionCoefficient;
    int numSamples;
    float coneSpread;
    float g;
    float lightIntensity;
    float time;
    int frame;
    int width;
    int height;
};

struct SceneUbo{
    int width;
    int height;
    int frame;
    float timeDelta;
    float elapsedTime;
    int numSamples;
    int currentSample;
    int bounces;
};

enum class Renderer :  int {
    RAY_MARCHING = 0, DELTA_TRACKING, PATH_TRACING
};


class OpenVdbViewer : public VulkanBaseApp{
public:
    explicit OpenVdbViewer(const Settings& settings = {});

protected:
    void initApp() override;

    void createPlaceHolderTexture();

    void initCamera();

    void initAnimation();

    void onFrameReady();

    void onFrameLoadFailure();

    void updateCamera();

    void createBuffers();

    void createRenderTarget();

    void createDescriptorPool();

    void createSamplers();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void updateVolumeDescriptorSets();

    void updateSceneDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void offscreenRender();

    void renderUI(VkCommandBuffer commandBuffer);

    void renderVolume(VkCommandBuffer commandBuffer);

    void renderBackground(VkCommandBuffer commandBuffer);

    void renderLight(VkCommandBuffer commandBuffer);

    void renderWithRayMarching(VkCommandBuffer commandBuffer);

    void renderWithDeltaTracking(VkCommandBuffer commandBuffer);

    void renderWithPathTracing(VkCommandBuffer commandBuffer);

    void renderFullscreenQuad(VkCommandBuffer commandBuffer);

    void renderVolumeSlices(VkCommandBuffer commandBuffer);

    bool openFileDialog();

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void fileInfo();

    void newFrame() override;

    void endFrame() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } rayMarching;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } deltaTracking;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } pathTracer;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } background;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        glm::vec3 position{0};
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        float scale{1};
        float intensity;
    } light;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } screenQuad;

    VulkanDescriptorPool cpuDescriptorPool;
    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<OrbitingCameraController> camera;
    std::string vdbPath;
    std::string startPath = R"(C:\Users\Josiah Ebhomenye\OneDrive\media\volumes\VDB-Clouds-Pack-Pixel-Lab\VDB Cloud Files)";


    VulkanDescriptorSetLayout descriptorSetLayout;
    VulkanDescriptorSetLayout volumeDescriptorSetLayout;
    VkDescriptorSet descriptorSet;
    VkDescriptorSet volumeDescriptor;
    uint32_t volumeBinding{1};
    std::vector<Frame> frames;

    Texture volumeTexture;
    Texture previousFrameTexture;

    VulkanDescriptorSetLayout sceneDescriptorSetLayout;
    VkDescriptorSet sceneDescriptorSet;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
        struct {
            glm::mat4 mvp{1};
            alignas(16) glm::vec3 viewDir{1};
            alignas(16) glm::vec3 scale{1};
            int numSlices{512};
        } constants;

    } sliceRenderer;

    LoadState loadState{LoadState::READY};

    VulkanBuffer cameraUboBuffer;
    VulkanBuffer volumeUboBuffer;
    VulkanBuffer vertexBuffer;
    VulkanBuffer placeHolderVertexBuffer;
    CameraUbo* cameraUbo{};
    VolumeUbo* volumeUbo{};
    VulkanBuffer uboBuffer;

    SceneUbo* sceneUbo{};
    VulkanBuffer sceneBuffer;

    Renderer renderer{Renderer::RAY_MARCHING};
    tf::Executor executor;
    tf::Taskflow loadVolumeFlow{};
    tf::Future<void> loadVolumeRequest;
    std::unique_ptr<Blur> blur;
    bool doBlur{true};
    int blurIterations{1};

    VulkanDescriptorSetLayout  renderDescriptorSetSetLayout;
    VkDescriptorSet renderDescriptorSet;

    struct {
        VulkanFramebuffer framebuffer;
        VulkanRenderPass renderPass;
    } renderTarget;

    VkPhysicalDeviceSynchronization2Features syncFeatures;
    std::vector<Texture> frameTextures;

    struct {
        ColorBuffer color;
        DepthBuffer  depth;
    } GBuffer;
    VulkanBuffer stagingBuffer;

    static constexpr int MAX_SAMPLES = 100000000;
    static constexpr int MaxFrames = std::numeric_limits<int>::max();
//    static constexpr int MaxFrames = 12;

    VulkanSampler linearSampler;
    std::unique_ptr<VdbAnimation> animation;
};