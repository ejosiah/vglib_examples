#include "../include/device/NeuralNetwork.hpp"

#include <algorithm>
#include <random>

#include "Barrier.hpp"
#include "filemanager.hpp"
#include "cpu/functions.hpp"

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
    , m_layers{layers.begin(), layers.end()}
    , m_params{params}
    , m_datasetDescriptorSetLayout{std::move(datasetDescriptorSetLayout)} {

    assert(m_layers.size() <= 8);

    refreshConstants();
    m_trainingDatasetDescriptorSet = std::get<0>(params.trainingData);
    m_trainingDataSet = std::get<1>(params.trainingData);

    if (params.testData.has_value()) {
        m_testDatasetDescriptorSet = std::get<0>(params.testData.value());
        m_testDataset = std::get<1>(params.testData.value());
    }

}

void dev::NeuralNetwork::init() {
    initNetwork();
    createDescriptorPool();
    createDescriptorSetLayout();
    updateDescriptorSets();
    createPipelines();
    spdlog::info("NeuralNetwork initialized");
}

void dev::NeuralNetwork::initNetwork() {
    auto rng = rngFn(m_params.testMode ? 1 << 20 : std::random_device{}());

    const auto bs = m_testDatasetDescriptorSet ? m_testDataset.labels.sizeAs<float>() : m_constants.batchSize;
    for (auto i = 0; i < m_layers.size(); ++i) {
        const auto L = m_layers[i];
        std::vector<float> activations(L * bs, 0.0f);
        auto activationBuffer = (i+1) ==
            m_layers.size() ? device->createCpuVisibleBuffer(activations.data(), BYTE_SIZE(activations), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)
                            : createBuffer(activations, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto deltaBuffer = createBuffer(BYTE_SIZE(activations), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        m_activations.push_back(activationBuffer);
        m_delta.push_back(deltaBuffer);

        if (i > 0) {
            std::vector<float> biases(L);
            std::ranges::generate(biases, [&]{ return rng(); });
            auto biasBuffer = createBuffer(biases, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nablaBiasBuffer = createBuffer(L * bs * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            m_biases.push_back(biasBuffer);
            m_nablaBiases.push_back(nablaBiasBuffer);

        }

        if (i+1 < m_layers.size()) {
            const auto L1 = m_layers[i + 1];
            std::vector<float> weights(L * L1);
            std::ranges::generate(weights, [&]{ return rng(); });
            auto weightsBuffer = createBuffer(weights, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto intermediateWeights = createBuffer(L1 * bs * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            auto nablaWeightsBuffer = createBuffer(L * L1 * bs * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            m_weights.push_back(weightsBuffer);
            m_nablaWeights.push_back(nablaWeightsBuffer);
            m_intermediateWeights.push_back(intermediateWeights);
        }
    }
}

void dev::NeuralNetwork::shuffleTrainingData(VkCommandBuffer commandBuffer) const {
    if (m_params.testMode) {
        const auto numBatches = std::max(1u, m_params.numBatches);
        m_testModeBatchOffset = (m_testModeBatchOffset + 1) % numBatches;
        return;
    }

    const auto numImages = std::max(1u, m_params.datasetSize);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("shuffle"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("shuffle"), 0, 1, &m_trainingDatasetDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("shuffle"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, 1, numImages, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::loadTrainingInputLayer(VkCommandBuffer commandBuffer) const {
    assert(m_trainingDataSet.images);
    assert(m_activations[0]);

    const auto& c = m_constants;
    const auto imageSize = m_layers[0];
    const VkBufferCopy region{
        .srcOffset = c.batchIndex * c.batchSize * imageSize * sizeof(float),
        .dstOffset = 0,
        .size = m_activations[0].size
    };
    vkCmdCopyBuffer(commandBuffer, m_trainingDataSet.images, m_activations[0], 1, &region);
    Barrier::transferWriteToComputeRead(commandBuffer);
}

void dev::NeuralNetwork::loadInputLayer(VkCommandBuffer commandBuffer, BufferRegion input) const {
    assert(m_trainingDataSet.images);
    assert(m_activations[0]);

    const VkBufferCopy region{
        .srcOffset = input.offset,
        .size = std::min(m_activations[0].size, input.size())
    };
    vkCmdCopyBuffer(commandBuffer, input.buffer->buffer, m_activations[0], 1, &region);
    Barrier::transferWriteToComputeRead(commandBuffer);
}

void dev::NeuralNetwork::clearNablaBuffers(VkCommandBuffer commandBuffer) const {
    for (const auto& buffer : m_nablaWeights) {
        vkCmdFillBuffer(commandBuffer, buffer, 0, buffer.size, 0);
    }

    for (const auto& buffer : m_nablaBiases) {
        vkCmdFillBuffer(commandBuffer, buffer, 0, buffer.size, 0);
    }

    Barrier::transferWriteToComputeWrite(commandBuffer);
}

void dev::NeuralNetwork::feedForward(VkCommandBuffer commandBuffer, uint numImages) {
    auto numLayers = m_layers.size();

    for (auto layer = 0; layer < numLayers - 1; ++layer) {
        m_constants.layerIndex = layer;
        const auto gy = m_layers[layer + 1];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("feed_forward"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("feed_forward"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("feed_forward"), 1, 1, &m_trainingDatasetDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, layout("feed_forward"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
        vkCmdDispatch(commandBuffer, 1, gy, numImages);
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
    vkCmdDispatch(commandBuffer, gx, m_constants.batchSize, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::computeHiddenDelta(VkCommandBuffer commandBuffer) const {
    const auto gx = m_layers[m_constants.layerIndex];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("compute_hidden_delta"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_hidden_delta"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("compute_hidden_delta"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, m_constants.batchSize, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::computeHiddenNablaBiases(VkCommandBuffer commandBuffer) const {
    constexpr uint32_t localSizeX = 1024;
    const auto size = m_layers[m_constants.layerIndex];
    const auto gx = nearestMultiple(size, localSizeX) / localSizeX;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("compute_hidden_nabla_biases"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_hidden_nabla_biases"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("compute_hidden_nabla_biases"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, m_constants.batchSize, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::computeHiddenNablaWeights(VkCommandBuffer commandBuffer) const {
    constexpr uint32_t localSizeX = 1024;
    const auto size = m_layers[m_constants.layerIndex - 1];
    const auto gx = nearestMultiple(size, localSizeX) / localSizeX;
    const auto gy = m_layers[m_constants.layerIndex];
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("compute_hidden_nabla_weights"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("compute_hidden_nabla_weights"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("compute_hidden_nabla_weights"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, gx, gy, m_constants.batchSize);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::computeBackPropagation(VkCommandBuffer commandBuffer) {
    const auto start = static_cast<int>(m_layers.size() - 2);
    for (auto layer = start; layer > 0; --layer) {
        m_constants.layerIndex = static_cast<uint>(layer);
        computeHiddenDelta(commandBuffer);
        computeHiddenNablaBiases(commandBuffer);
        computeHiddenNablaWeights(commandBuffer);
    }
}

void dev::NeuralNetwork::reduceNablaWeights(VkCommandBuffer commandBuffer) const {
    const auto layers = m_layers.size() - 1;
    size_t maxSize = 0;
    for (size_t layer = 0; layer < layers; ++layer) {
        maxSize = std::max(maxSize, static_cast<size_t>(m_layers[layer]) * m_layers[layer + 1]);
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("reduce_nabla_weights"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("reduce_nabla_weights"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("reduce_nabla_weights"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, 1, static_cast<uint32_t>(maxSize), static_cast<uint32_t>(layers));
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::reduceNablaBiases(VkCommandBuffer commandBuffer) const {
    const auto layers = m_layers.size() - 1;
    const auto maxSize = *std::max_element(m_layers.begin() + 1, m_layers.end());
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("reduce_nabla_biases"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("reduce_nabla_biases"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("reduce_nabla_biases"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, 1, maxSize, static_cast<uint32_t>(layers));
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::updateWeights(VkCommandBuffer commandBuffer) const {
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
    reduceNablaWeights(commandBuffer);
    reduceNablaBiases(commandBuffer);
    updateWeights(commandBuffer);
    updateBiases(commandBuffer);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::evaluateClassificationRate(VkCommandBuffer commandBuffer) {
    if (!m_testDatasetDescriptorSet) return;

    const auto trainingBatchSize = m_constants.batchSize;
    const auto trainingBatchIndex = m_constants.batchIndex;
    m_constants.batchSize = static_cast<uint>(m_testDataset.images.sizeAs<float>() / m_layers[0]);
    m_constants.batchIndex = 0;

    evaluate(commandBuffer, m_testDataset.images);
    assertTestOutput(commandBuffer);
    printClassificationRate(commandBuffer);

    m_constants.batchSize = trainingBatchSize;
    m_constants.batchIndex = trainingBatchIndex;
}

void dev::NeuralNetwork::assertTestOutput(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("assert_test_output"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("assert_test_output"), 0, 1, &m_neuralNetworkDescriptorSet, 0, nullptr);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("assert_test_output"), 1, 1, &m_testDatasetDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("assert_test_output"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, nearestMultiple(m_constants.batchSize, 1024u) / 1024u, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::printClassificationRate(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline("print_classification_rate"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout("print_classification_rate"), 1, 1, &m_testDatasetDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout("print_classification_rate"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_constants), &m_constants);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void dev::NeuralNetwork::evaluate(VkCommandBuffer commandBuffer, VulkanBuffer input) {
    evaluate(commandBuffer, input.region(0));
}

void dev::NeuralNetwork::evaluate(VkCommandBuffer commandBuffer, BufferRegion input) {
    const auto numInputs = input.sizeAs<float>() / m_layers[0];
    loadInputLayer(commandBuffer, input);
    feedForward(commandBuffer, numInputs);
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



void dev::NeuralNetwork::train(VkCommandBuffer commandBuffer) {
    for (auto j = 0; j < m_params.epochs; ++j) {
        train(commandBuffer, j);
    }
}

void dev::NeuralNetwork::train(VkCommandBuffer commandBuffer, uint epoch) {
    m_constants.epoch = epoch;

    device->group([&] {
        shuffleTrainingData(commandBuffer);
        for (auto k = 0; k < m_params.numBatches; ++k) {
            updateBatch(commandBuffer, k);
        }
    }, commandBuffer, "gradient_descent", {1, 0, 0, 1});

    device->group([&] {
        evaluateClassificationRate(commandBuffer);
    }, commandBuffer, "classification_rate", {0, 0, 1, 1});
}

void dev::NeuralNetwork::updateBatch(VkCommandBuffer commandBuffer, uint batchIndex) {
    if (m_params.testMode) {
        const auto numBatches = std::max(1u, m_params.numBatches);
        m_constants.batchIndex = (m_testModeBatchOffset + batchIndex) % numBatches;
    } else {
        m_constants.batchIndex = batchIndex;
    }
    clearNablaBuffers(commandBuffer);
    loadTrainingInputLayer(commandBuffer);
    feedForward(commandBuffer, m_constants.batchSize);
    computeOutputActivationDelta(commandBuffer);
    computeBackPropagation(commandBuffer);
    updateWeightsAndBiases(commandBuffer);
}

void dev::NeuralNetwork::refreshConstants() {
    for (auto i = 0; i < m_layers.size(); ++i) {
        m_constants.layers[i] = m_layers[i];
    }
    m_constants.numLayers = m_layers.size();
    m_constants.epoch = m_params.epochs;
    const auto numBatches = std::max(1u, m_params.numBatches);
    m_constants.batchSize = std::max(1u, m_params.datasetSize / numBatches);
    m_constants.batchIndex = 0;
    m_constants.layerIndex = 0;
    m_constants.eta = m_params.eta;
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
            "shuffle",
            resource("shuffle.comp.spv"),
            { &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
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
            "compute_hidden_delta",
            resource("compute_hidden_delta.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "compute_hidden_nabla_biases",
            resource("compute_hidden_nabla_biases.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "compute_hidden_nabla_weights",
            resource("compute_hidden_nabla_weights.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "update_weights",
            resource("update_weights.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "reduce_nabla_weights",
            resource("reduce_nabla_weights.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "reduce_nabla_biases",
            resource("reduce_nabla_biases.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "assert_test_output",
            resource("assert_test_output.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Constants) } }
        },
        {
            "print_classification_rate",
            resource("print_classification_rate.comp.spv"),
            { &m_neuralNetworkDescriptorSetLayout, &m_datasetDescriptorSetLayout },
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

