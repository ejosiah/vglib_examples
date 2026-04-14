#include "../include/device/NeuralNetwork.hpp"

#include <algorithm>
#include <random>

#include "Barrier.hpp"
#include "filemanager.hpp"

auto resource(const std::string& name) {
    const auto res = FileManager::instance().getFullPath(name);
    assert(res.has_value());
    return res->string();
}


dev::NeuralNetwork::NeuralNetwork(
    VulkanDevice *device,
    VulkanDescriptorSetLayout datasetDescriptorSetLayout,
    std::initializer_list<uint> layers,
    const Params& params
    )
    : ComputePipelines{device}
    , m_device{device}
    , m_datasetDescriptorSetLayout{std::move(datasetDescriptorSetLayout)}
    , m_layers{layers.begin(), layers.end()}
    , m_params{params} {

    assert(m_layers.size() <= 8);
    for (auto i = 0; i < m_layers.size(); ++i) {
        m_constants.layers[i] = m_layers[i];
    }
    m_constants.numLayers = m_layers.size();
    m_constants.epoch = params.epochs;
    const auto numBatches = std::max(1u, params.numBatches);
    m_constants.batchSize = std::max(1u, params.datasetSize / numBatches);
    m_constants.batchIndex = 0;
    m_constants.layerIndex = 0;
    m_constants.eta = params.eta;
    m_trainingDatasetDescriptorSet = std::get<0>(params.trainingData);
    m_trainingDataSet = std::get<1>(params.trainingData);

}

void dev::NeuralNetwork::init() {
    initNetwork();
    createDescriptorPool();
    createDescriptorSetLayout();
    updateDescriptorSets();
    createPipelines();
}

void dev::NeuralNetwork::initNetwork() {
    std::normal_distribution<float> distribution{0.0f, 1.0f};
    std::default_random_engine generator{ m_params.hostVisible ? 1 << 20 : std::random_device{}()};
    auto rng = std::bind(distribution, generator);

    for (auto i = 0; i < m_layers.size(); ++i) {
        const auto L = m_layers[i];
        std::vector<float> activations(L, 0.0f);
        auto activationBuffer = createBuffer(activations, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto deltaBuffer = createBuffer(BYTE_SIZE(activations), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        m_activations.push_back(activationBuffer);
        m_delta.push_back(deltaBuffer);

        if (i > 0) {
            std::vector<float> biases(L);
            std::ranges::generate(biases, [&]{ return rng(); });
            auto biasBuffer = createBuffer(biases, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nablaBiasBuffer = createBuffer(BYTE_SIZE(biases), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            m_biases.push_back(biasBuffer);
            m_nablaBiases.push_back(nablaBiasBuffer);

        }

        if (i+1 < m_layers.size()) {
            const auto L1 = m_layers[i + 1];
            std::vector<float> weights(L * L1);
            std::ranges::generate(weights, [&]{ return rng(); });
            auto weightsBuffer = createBuffer(weights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto intermediateWeights = createBuffer(L1 * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nablaWeightsBuffer = createBuffer(BYTE_SIZE(weights), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            m_weights.push_back(weightsBuffer);
            m_nablaWeights.push_back(nablaWeightsBuffer);
            m_intermediateWeights.push_back(intermediateWeights);
        }
    }
}

void dev::NeuralNetwork::shuffleTrainingData(VkCommandBuffer commandBuffer) {
}

void dev::NeuralNetwork::loadInputLayer(VkCommandBuffer commandBuffer) {
    assert(m_trainingDataSet);
    assert(m_activations[0]);

    const auto& c = m_constants;
    const auto imageSize = m_layers[0];
    VkBufferCopy region{
        .srcOffset = c.batchIndex * c.batchSize * imageSize * sizeof(float),
        .dstOffset = 0,
        .size = m_activations[0].size
    };
    vkCmdCopyBuffer(commandBuffer, m_trainingDataSet, m_activations[0], 1, &region);
    Barrier::transferWriteToComputeRead(commandBuffer);
}

void dev::NeuralNetwork::feedForward(VkCommandBuffer commandBuffer) {
    auto numLayers = m_layers.size();

    for (auto layer = 0; layer < numLayers - 1; ++layer) {
        m_constants.layerIndex = layer;
        const auto gx = m_layers[layer + 1];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("feed_forward"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("feed_forward"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("feed_forward"), 1, 1, &m_trainingDatasetDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, layout("feed_forward"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
        Barrier::computeWriteToRead(commandBuffer);
    }
}

void dev::NeuralNetwork::computeOutputActivationDelta(VkCommandBuffer commandBuffer) {
    const auto last = m_layers.size() - 1;
    const auto outputLayer = last - 1;
    m_constants.layerIndex = static_cast<uint>(outputLayer);

    const auto gx = m_layers[outputLayer + 1];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("compute_output_activation_delta"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_output_activation_delta"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_output_activation_delta"), 1, 1, &m_trainingDatasetDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("compute_output_activation_delta"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::computeBackPropagation(VkCommandBuffer commandBuffer) {
    const auto start = static_cast<int>(m_layers.size() - 2);
    for (auto layer = start; layer > 0; --layer) {
        m_constants.layerIndex = static_cast<uint>(layer);
        const auto gx = m_layers[m_constants.layerIndex];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("back_propagation"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("back_propagation"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, layout("back_propagation"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
        Barrier::computeWriteToRead(commandBuffer);
    }
}

void dev::NeuralNetwork::updateWeights(VkCommandBuffer commandBuffer) {
    constexpr uint32_t localSizeX = 1024;
    const auto layers = m_layers.size() - 1;
    size_t maxSize = 0;
    for (size_t layer = 0; layer < layers; ++layer) {
        maxSize = std::max(maxSize, static_cast<size_t>(m_layers[layer]) * m_layers[layer + 1]);
    }
    const auto gx = nearestMultiple(static_cast<uint>(maxSize), localSizeX) / localSizeX;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("update_weights"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("update_weights"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("update_weights"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, static_cast<uint32_t>(layers), 1);
}

void dev::NeuralNetwork::updateBiases(VkCommandBuffer commandBuffer) {
    constexpr uint32_t localSizeX = 1024;
    const auto layers = m_layers.size() - 1;
    const auto maxSize = *std::max_element(m_layers.begin() + 1, m_layers.end());
    const auto gx = nearestMultiple(maxSize, localSizeX) / localSizeX;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("update_biases"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("update_biases"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("update_biases"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, static_cast<uint32_t>(layers), 1);
}

void dev::NeuralNetwork::updateWeightsAndBiases(VkCommandBuffer commandBuffer) {
    updateWeights(commandBuffer);
    updateBiases(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);

}


void dev::NeuralNetwork::train(VkCommandBuffer commandBuffer, const Params& params) {
    throw std::runtime_error("dev::NeuralNetwork::train not yet implemented!");
}


void dev::NeuralNetwork::createDescriptorPool() {
    constexpr uint32_t maxSets = 10;
    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets},
    }};

    m_descriptorPool = m_device->createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void dev::NeuralNetwork::createDescriptorSetLayout() {
    m_neuralNetworkDescriptorSetLayout = 
        device->descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_weights))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_biases))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_activations))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_intermediateWeights))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_delta))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_nablaWeights))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(COUNT(m_nablaBiases))
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void dev::NeuralNetwork::updateDescriptorSets() {
        auto sets = m_descriptorPool.allocate({ m_neuralNetworkDescriptorSetLayout });
        m_neuralNetworkDescriptorSet = sets[0];

        auto writes = initializers::writeDescriptorSets<7>();

        auto descriptorInfo = [](const auto& buffer) {
            return VkDescriptorBufferInfo{ buffer, 0, VK_WHOLE_SIZE };
        };

        auto weightsInfo = map_range(m_weights, descriptorInfo);
        auto biasesInfo = map_range(m_biases, descriptorInfo);
        auto activationsInfo = map_range(m_activations, descriptorInfo);
        auto intermediateWeightsInfo = map_range(m_intermediateWeights, descriptorInfo);
        auto deltaInfo = map_range(m_delta, descriptorInfo);
        auto nablaWeightsInfo = map_range(m_nablaWeights, descriptorInfo);
        auto nablaBiasesInfo = map_range(m_nablaBiases, descriptorInfo);

        writes[0].dstSet = m_neuralNetworkDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = COUNT(weightsInfo);
        writes[0].pBufferInfo = weightsInfo.data();

        writes[1].dstSet = m_neuralNetworkDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = COUNT(biasesInfo);
        writes[1].pBufferInfo = biasesInfo.data();

        writes[2].dstSet = m_neuralNetworkDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = COUNT(activationsInfo);
        writes[2].pBufferInfo = activationsInfo.data();

        writes[3].dstSet = m_neuralNetworkDescriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = COUNT(intermediateWeightsInfo);
        writes[3].pBufferInfo = intermediateWeightsInfo.data();

        writes[4].dstSet = m_neuralNetworkDescriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = COUNT(deltaInfo);
        writes[4].pBufferInfo = deltaInfo.data();

        writes[5].dstSet = m_neuralNetworkDescriptorSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = COUNT(nablaWeightsInfo);
        writes[5].pBufferInfo = nablaWeightsInfo.data();

        writes[6].dstSet = m_neuralNetworkDescriptorSet;
        writes[6].dstBinding = 6;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = COUNT(nablaBiasesInfo);
        writes[6].pBufferInfo = nablaBiasesInfo.data();

        device->updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> dev::NeuralNetwork::pipelineMetaData() {
    return
    {
        {
            "load_input_layer",
            resource("load_input_layer.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "feed_forward",
            resource("feed_forward.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "compute_output_activation_delta",
            resource("compute_output_activation_delta.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "back_propagation",
            resource("back_propagation.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "update_weights",
            resource("update_weights.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "update_biases",
            resource("update_biases.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        }
    };
}

