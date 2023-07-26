#pragma once

#include "VulkanBaseApp.h"
#include "VulkanDrawable.hpp"

class ShadowVolumeDemo : public VulkanBaseApp {
public:
    ShadowVolumeDemo(const Settings& settings = {});

protected:
    void initApp() final ;

    void initUBO();

    void initBuffers();

    void createDescriptorPool();

    void createDescriptorSet();

    void updateDescriptorSet();

    void createCommandPool();

    void createPipeline();

    void onSwapChainDispose() final;

    void onSwapChainRecreation() final;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) final;

    void renderSceneIntoDepthBuffer(VkCommandBuffer commandBuffer);

    void renderSceneShadowVolumeIntoStencilBuffer(VkCommandBuffer commandBuffer);

    void renderSilhouette(VkCommandBuffer commandBuffer);

    void visualizeShadowVolume(VkCommandBuffer commandBuffer);

    void renderAmbientLight(VkCommandBuffer commandBuffer);

    void renderScene(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void newFrame() final;

    void endFrame() final;

    void update(float time) final;

    void checkAppInputs() final;

    void cleanup() final;

    void onPause() final;

    void initCamera();

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } depthOnly;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } ambient;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } shadow_volume;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } silhouette;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } shadow_volume_visual;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } compute;

    VulkanDrawable plane;
    VulkanDrawable cube;

    struct UBO{
        glm::vec3 lightPosition;
        glm::vec3 cameraPosition;
    };

    VulkanBuffer uboBuffer;

    UBO* ubo{};

    VulkanDrawable model;

    bool showSilhouette = false;
    bool showShadowVolume = false;

    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<FirstPersonCameraController> camera;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeature;

    VkPhysicalDeviceColorWriteEnableFeaturesEXT colorWriteEnabledFeature;

    glm::mat4 xform{1};
    glm::mat4 xform1{1};

    static constexpr VkShaderStageFlags ALL_SHADER_STAGES =
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
            | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
};