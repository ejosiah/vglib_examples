#pragma once

#include "Offscreen.hpp"
#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "VulkanRayQuerySupport.hpp"
#include "gltf/Bvh.hpp"
#include "ComputePipelins.hpp"

class RtxDemo : public VulkanBaseApp {
public:
    explicit RtxDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void initCamera();

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

    void createRenderPipeline();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void depthPrepass(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer, const Pipeline& pipeline);

    void visualizeDepthBuffer(VkCommandBuffer commandBuffer);

    void renderFullscreenQuad(VkCommandBuffer commandBuffer, uint textureIndex = 0);

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
    } render;

    Offscreen::RenderInfo renderInfo;
    Offscreen::RenderInfo dppRenderInfo;
    Texture colorBuffer;
    Texture normalBuffer;
    Texture depthBuffer;

    uint32_t depthBufferIndex{~0u};
    uint32_t colorBufferIndex{~0u};

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;
    std::shared_ptr<gltf::Model> scene;
    gltf::bvh::Bvh bvh;

    std::span<gltf::Light> lights;
    std::span<gltf::LightInstance> lightInstances;

    struct UniformData {
        glm::mat4 projection{1};
        glm::mat4 view{1};
        glm::mat4 model{1};
        glm::mat4 inverseProjection{1};
        glm::mat4 inverseView{1};
        glm::mat4 previousViewProjection{1};
        glm::vec2 viewportSize{};
        float near;
        float far;
    };

    struct {
        VulkanBuffer gpu;
        UniformData* cpu;
    } uniforms;

    VulkanDescriptorSetLayout uniformDescriptorSetLayout;
    VkDescriptorSet uniformDescriptorSet{};
};