#pragma once

#include "gltf/GltfLoader.hpp"
#include "VulkanBaseApp.h"
#include "mnist/mnist_loader.hpp"
#include "device/NeuralNetwork.hpp"

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

    void onSwapChainDispose() override;

    void onSwapChainRecreation() override;

    VkCommandBuffer *buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) override;

    void update(float time) override;

    void checkAppInputs() override;

    void cleanup() override;

    void onPause() override;

protected:
    struct {
        VulkanPipelineLayout layout;
        VulkanPipeline pipeline;
    } render;

    VulkanDescriptorPool descriptorPool;
    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanPipelineCache pipelineCache;
    std::unique_ptr<BaseCameraController> camera;
    std::unique_ptr<gltf::Loader> loader;
    BindlessDescriptor bindlessDescriptor;

    VulkanDescriptorSetLayout datasetDescriptorSetLayout;
    VkDescriptorSet trainingDatasetDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet testDatasetDescriptorSet{VK_NULL_HANDLE};
    VulkanBuffer trainingImages;
    VulkanBuffer trainingLabels;
    std::array<VulkanBuffer, 2> trainingLocks;
    VulkanBuffer testImages;
    VulkanBuffer testLabels;
    VulkanBuffer fullscreenQuad;
    mnist::Header trainingSetHeader;
    mnist::Header testSetHeader;
    dev::NeuralNetwork network;

    struct {
        uint32_t width{};
        uint32_t height{};
        uint32_t imageCount{};
        uint32_t offset{};
    } constants;
};
