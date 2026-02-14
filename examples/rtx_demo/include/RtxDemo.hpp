#pragma once

#include "Offscreen.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "VulkanRayQuerySupport.hpp"
#include "gltf/Bvh.hpp"
#include "ComputePipelins.hpp"
#include "CameraInfo.hpp"
#include "rtx/shadow.hpp"
#include "Sampler.hpp"

class RtxDemo : public VulkanBaseApp {
public:
    explicit RtxDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

    void initShadow();

    void loadScene();

    void initBuffers();

    void initUniforms();

    void initRenderInfo();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void initLights();

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void depthPrepass(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer, const Pipeline& pipeline);

    void visualizeDepthBuffer(VkCommandBuffer commandBuffer);

    void toneMap(VkCommandBuffer commandBuffer);

    void renderFullscreenQuad(VkCommandBuffer commandBuffer, uint textureIndex = 0);

    void renderLights(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    void endFrame() override;

    struct {
        Pipeline pbr;
        Pipeline prePass;
        Pipeline fullscreen;
        Pipeline depthBufferVis;
        Pipeline lights;
        Pipeline toneMap;
    } render;

    Offscreen::RenderInfo renderInfo;
    Offscreen::RenderInfo dppRenderInfo;
    Texture colorBuffer;
    Texture normalBuffer;
    Texture depthBuffer;

    uint32_t colorBufferIndex{~0u};
    uint32_t normalBufferIndex{~0u};
    uint32_t depthBufferIndex{~0u};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::shared_ptr<gltf::Model> scene;
    gltf::bvh::Bvh bvh;



    struct {
        std::span<gltf::Light> lights;
        std::span<gltf::LightInstance> lightInstances;
        VulkanBuffer lightBuffer;
        VulkanBuffer lightInstanceBuffer;
        VulkanDescriptorSetLayout descriptorSetLayout;
        VkDescriptorSet descriptorSet{};
        uint32_t numLights{1};
    } lightInfo;

    struct UniformData {
        int dummy;
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu;
    } uniforms;

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VkDescriptorSet uniformDescriptorSet{};
    std::shared_ptr<CameraInfo> cameraInfo;
    rtx::shadow shadow;
    struct {
        VulkanBuffer vertices;
        VulkanBuffer indexes;
    } sphere;

    Jitter jitter{};
    glm::vec2 jitterValue{};
};