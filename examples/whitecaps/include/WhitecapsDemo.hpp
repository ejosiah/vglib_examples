#pragma once

#include "VulkanBaseApp.h"
#include "ComputePipelins.hpp"
#include "Texture.h"
#include "Profiler.hpp"

class WhitecapsDemo : public VulkanBaseApp {
public:
    explicit WhitecapsDemo(const Settings& settings = {});

protected:
    void initApp() override;
    void beforeDeviceCreation() override;
    void onSwapChainDispose() override;
    void onSwapChainRecreation() override;
    VkCommandBuffer* buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) override;
    void update(float time) override;
    void checkAppInputs() override;
    void cleanup() override;

private:
    static constexpr uint32_t Passes = 8;
    static constexpr uint32_t FftSize = 1u << Passes;
    static constexpr uint32_t WaveLayers = 8;
    static constexpr uint32_t SlopeVarianceSize = 4;

    struct Controls {
        glm::vec4 gridSizes{893.0f, 101.0f, 21.0f, 11.0f};
        glm::vec4 choppy{2.3f, 2.1f, 1.3f, 0.9f};
        glm::vec4 seaColor{11.0f / 255.0f, 121.0f / 255.0f, 49.0f / 255.0f, 140.0f / 255.0f};
        glm::vec4 cloudColor{1.0f};
        glm::vec4 sunDirection{0.0f, 1.0f, 0.2f, 0.0f};
        float wind{12.0f};
        float omega{2.0f};
        float amplitude{2.0f};
        float time{0.0f};
        float exposure{1.05f};
        float jacobianScale{0.2f};
        float gridSize{4.0f};
        float showSpectrumZoom{1.0f};
        int showSpectrumLinear{0};
        int flags{0};
        int pass{0};
        int layerOffset{0};
    };

    struct Pipelines {
        Pipeline render;
        Pipeline sky;
        Pipeline skymap;
        Pipeline clouds;
        Pipeline spectrum;
        Pipeline composite;
    };

    struct SimTextures {
        Texture spectrum12;
        Texture spectrum34;
        Texture waves[2];
        Texture butterfly;
        Texture slopeVariance;
        Texture whitecaps;
        Texture sky;
        Texture noise;
        Texture irradiance;
        Texture inscatter;
        Texture transmittance;
    };

    void createCommandPool();
    void createDescriptorPool();
    void createTextures();
    void createDescriptorSetLayout();
    void updateDescriptorSet();
    void createComputePipelines();
    void createGraphicsPipelines();
    void createCommandBuffers();
    void createGrid();
    void createButterflyLookup();
    void loadAtmosphereTables();
    void recordSimulation(VkCommandBuffer commandBuffer);
    void renderScene(VkCommandBuffer commandBuffer);
    void renderUi(VkCommandBuffer commandBuffer);
    std::vector<PipelineMetaData> computeMetadata();

    VulkanCommandPool commandPool;
    std::vector<VkCommandBuffer> commandBuffers;
    VulkanDescriptorPool descriptorPool;
    VulkanDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet{};
    VulkanPipelineCache pipelineCache;
    ComputePipelines compute;
    Pipelines pipelines;
    SimTextures textures;
    VulkanBuffer gridVertices;
    VulkanBuffer gridIndexes;
    uint32_t gridIndexCount{};
    Controls controls;
    Profiler profiler;
    float elapsedTime{};
    int pingPong{};
};
