#pragma once

#include <imgui.h>

#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "Profiler.hpp"
#include "mnist/mnist_loader.hpp"
#include "device/NeuralNetwork.hpp"
#include "ComputePipelins.hpp"
#include <glm/glm.hpp>

class NeuralNetworksDemo : public VulkanBaseApp{
public:
    explicit NeuralNetworksDemo(const Settings& settings = {});

protected:
    void initApp() override;

    void loadDataset();

    void initNetwork();

    void initCamera();

    void initBindlessDescriptor();

    void beforeDeviceCreation() override;

    void createDescriptorPool();

    void createDescriptorSetLayouts();

    void updateDescriptorSets();

    void createCommandPool();

    void createPipelineCache();

    void initLoader();

    void createRenderPipeline();

    void createComputePipelines();

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void renderCanvas(VkCommandBuffer commandBuffer);

    void renderTrainingData(VkCommandBuffer commandBuffer);

    void renderUI(VkCommandBuffer commandBuffer);

    void canvasToInput(VkCommandBuffer commandBuffer);

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

    void newFrame() override;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } canvas;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    ComputePipelines computePipelines;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    VulkanDescriptorSetLayout datasetDescriptorSetLayout;
    VulkanDescriptorSetLayout canvasDescriptorSetLayout;
    VulkanDescriptorSetLayout canvasToInputDescriptorSetLayout;
    VkDescriptorSet trainingDatasetDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet testDatasetDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet canvasDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet canvasToInputDescriptorSet{VK_NULL_HANDLE};
    VulkanBuffer trainingImages;
    VulkanBuffer trainingLabels;
    VulkanBuffer results;
    std::array<VulkanBuffer, 2> trainingLocks;
    VulkanBuffer testImages;
    VulkanBuffer testLabels;
    VulkanBuffer fullscreenQuad;
    mnist::Header trainingSetHeader;
    mnist::Header testSetHeader;
    dev::NeuralNetwork network;
    Profiler profiler;

    VulkanBuffer canvasBuffer;
    VulkanBuffer inputBuffer;
    Texture inputTexture;
    ImTextureID inputTextureID;
    std::span<float> output;

    struct {
        glm::vec2 mousePos{0};
        int mouseClicked{0};
        uint32_t width{};
        uint32_t height{};
        uint32_t imageCount{};
        uint32_t offset{};
    } constants;

    struct CanvasConstants {
        glm::vec2 center{0.5};
        float radius{0.01};
        int active{0};
        int clear{0};
    } canvasConstants;

    struct CanvasToInputConstants {
        glm::uvec2 canvasSize{280, 280};
        glm::uvec2 inputSize{28, 28};
    } canvasToInputConstants;
    bool shouldEvaluate{false};
};
