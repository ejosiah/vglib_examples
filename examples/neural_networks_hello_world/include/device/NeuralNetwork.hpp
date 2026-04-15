#pragma once

#include "ComputePipelins.hpp"
#include "VulkanDevice.h"

#include <initializer_list>
#include <optional>
#include <array>

namespace dev {
    class NeuralNetwork final : public ComputePipelines {
    public:
        struct Dataset final { VulkanBuffer images, labels; };
        friend class NeuralNetworkFixture;
        struct Params {
            std::tuple<VkDescriptorSet, Dataset> trainingData{};
            uint epochs{1};
            uint numBatches{1};
            uint datasetSize{1};
            float eta{1.0};
            std::optional<std::tuple<VkDescriptorSet, Dataset>> testData{};
            bool testMode{};
        };

        NeuralNetwork() = default;

        NeuralNetwork(VulkanDevice* device,
                      VulkanDescriptorSetLayout datasetDescriptorSetLayout,
                      std::initializer_list<uint> layers,
                      const Params& params);

        void init();

        void initNetwork();

        void shuffleTrainingData(VkCommandBuffer commandBuffer) const;

        void loadInputLayer(VkCommandBuffer commandBuffer) const;

        void clearNablaBuffers(VkCommandBuffer commandBuffer) const;

        void feedForward(VkCommandBuffer commandBuffer);

        void computeOutputActivationDelta(VkCommandBuffer commandBuffer);

        void computeBackPropagation(VkCommandBuffer commandBuffer);

        void reduceNablaWeights(VkCommandBuffer commandBuffer) const;

        void reduceNablaBiases(VkCommandBuffer commandBuffer) const;

        void updateWeights(VkCommandBuffer commandBuffer) const;

        void updateBiases(VkCommandBuffer commandBuffer);

        void updateWeightsAndBiases(VkCommandBuffer commandBuffer);

        void train(VkCommandBuffer commandBuffer);

        void updateBatch(VkCommandBuffer commandBuffer, uint batchIndex);

        void refreshConstants();

    protected:
        void createDescriptorPool();

        void createDescriptorSetLayout();

        void updateDescriptorSets();

        std::vector<PipelineMetaData> pipelineMetaData() override;

        template<typename T>
        VulkanBuffer createBuffer(const std::vector<T>& source, VkBufferUsageFlags usage) {
            if (m_params.testMode) {
                return device->createCpuVisibleBuffer(source.data(), BYTE_SIZE(source), usage);
            }
            return device->createDeviceLocalBuffer(source.data(), BYTE_SIZE(source), usage);
        }

        VulkanBuffer createBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage) {
            auto memoryUsage = m_params.testMode ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY;
            return device->createBuffer(usage, memoryUsage, size);
        }

    public:
        VulkanDevice* m_device{};
        std::vector<uint> m_layers{};
        std::vector<VulkanBuffer> m_weights;
        std::vector<VulkanBuffer> m_biases;
        std::vector<VulkanBuffer> m_activations;
        std::vector<VulkanBuffer> m_intermediateWeights;
        std::vector<VulkanBuffer> m_delta;
        std::vector<VulkanBuffer> m_nablaBiases;
        std::vector<VulkanBuffer> m_nablaWeights;
        Params m_params{};

        VulkanDescriptorPool m_descriptorPool;
        VulkanDescriptorSetLayout m_datasetDescriptorSetLayout;
        VulkanDescriptorSetLayout m_neuralNetworkDescriptorSetLayout;
        VkDescriptorSet m_trainingDatasetDescriptorSet{VK_NULL_HANDLE};
        VkDescriptorSet m_testDatasetDescriptorSet{VK_NULL_HANDLE};
        VkDescriptorSet m_neuralNetworkDescriptorSet{VK_NULL_HANDLE};

        Dataset m_trainingDataSet;
        Dataset m_testDataset;

        struct Constants {
            std::array<uint, 8> layers{};
            uint epoch{0};
            uint layerIndex{0};
            uint batchIndex{0};
            uint batchSize{1};
            uint numLayers{1};
            float eta{1.0};
        } m_constants;
    };
}
