#include <gtest/gtest.h>
#include "device/NeuralNetwork.hpp"
#include "cpu/NeuralNetwork.hpp"
#include <vulkan_context.hpp>
#include <filemanager.hpp>
#include <mnist/mnist_loader.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <random>
#include <type_traits>

#include "Barrier.hpp"

class NeuralNetworkFixture : public ::testing::Test {
protected:
    static mnist::Dataset s_trainingDataset;
    static mnist::Dataset s_testDataset;
    static bool s_datasetLoaded;

    std::unique_ptr<VulkanContext> context;
    ComputePipelines compute;
    dev::NeuralNetwork::Constants constants{};
    dev::NeuralNetwork network;

    struct {
        mnist::Dataset trainingDataset;
        mnist::Dataset testDataset;
        std::vector<size_t> layers;
        std::vector<std::vector<float>> weights;
        std::vector<std::vector<float>> biases;
        std::vector<std::vector<float>> activations;
        std::vector<std::vector<float>> intermediateWeights;
        std::vector<std::vector<float>> delta;
        std::vector<std::vector<float>> nablaBiases;
        std::vector<std::vector<float>> nablaWeights;
    } host;

    struct {
        std::vector<VulkanBuffer> weights;
        std::vector<VulkanBuffer> biases;
        std::vector<VulkanBuffer> activations;
        std::vector<VulkanBuffer> intermediateWeights;
        std::vector<VulkanBuffer> delta;
        std::vector<VulkanBuffer> nablaBiases;
        std::vector<VulkanBuffer> nablaWeights;
    } device;


    VulkanDescriptorPool descriptorPool;
    VulkanDescriptorSetLayout datasetDescriptorSetLayout;
    VulkanDescriptorSetLayout neuralNetworkDescriptorSetLayout;
    VkDescriptorSet trainingDatasetDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet testDatasetDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet neuralNetworkDescriptorSet{VK_NULL_HANDLE};
    VulkanBuffer trainingImages;
    VulkanBuffer trainingLabels;
    std::array<VulkanBuffer, 2> trainingLocks;
    VulkanBuffer testImages;
    VulkanBuffer testLabels;

    void SetUp() override {
        spdlog::set_level(spdlog::level::info);
        updateSearchPath();
        loadDataset();
        initHostNetwork({784, 30, 10});
        initVulkan();
        createDescriptorPool();
        createDeviceBuffers();
        createDescriptorSetLayouts();
        updateDescriptorSets();
        initCompute();
        initDeviceNetwork();
    }

    void TearDown() override {
    }

    void initHostNetwork(const std::initializer_list<uint> layers) {
        host.layers.assign(layers.begin(), layers.end());
        host.activations.clear();
        host.biases.clear();
        host.weights.clear();
        host.intermediateWeights.clear();
        host.delta.clear();
        host.nablaBiases.clear();
        host.nablaWeights.clear();
        constants = {};

        std::normal_distribution<float> distribution{0.0f, 1.0f};
        std::default_random_engine generator{ 1 << 20 };
        auto rng = std::bind(distribution, generator);

        for (size_t i = 0; i < host.layers.size(); ++i) {
            const auto layerSize = host.layers[i];
            constants.layers[i] = layerSize;

            host.activations.emplace_back(layerSize, 0.0f);
            host.delta.emplace_back(layerSize, 0.0f);

            if (i > 0) {
                auto& biases = host.biases.emplace_back(layerSize);
                std::ranges::generate(biases, [&] { return rng(); });

                host.nablaBiases.emplace_back(layerSize, 0.0f);
            }

            if (i + 1 < host.layers.size()) {
                const auto nextLayerSize = host.layers[i + 1];
                const auto weightCount = layerSize * nextLayerSize;
                auto& weights = host.weights.emplace_back(weightCount);
                std::ranges::generate(weights, [&] { return rng(); });

                host.intermediateWeights.emplace_back(nextLayerSize, 0.0f);
                host.nablaWeights.emplace_back(weightCount, 0.0f);
            }
        }

        constants.numLayers = static_cast<uint>(host.layers.size());
        constants.batchSize = 1;
        constants.eta = 3.0f;
    }

    void initDeviceNetwork() {
        network = dev::NeuralNetwork{ &context->device, datasetDescriptorSetLayout, {784, 30, 10},  {
            .trainingData = std::make_tuple(trainingDatasetDescriptorSet, trainingImages),
            .epochs = 1,
            .numBatches = 1,
            .datasetSize = 1,
            .eta = 3.0f,
            .hostVisible = true
        }};
        network.init();
    }

    void loadDataset() {
        if (!s_datasetLoaded) {
            s_trainingDataset = mnist::load(resource("mnist_dataset/train-images.idx3-ubyte"),
                                            resource("mnist_dataset/train-labels.idx1-ubyte"));

            s_testDataset.header = s_trainingDataset.header;

            const auto imageSize = static_cast<size_t>(s_trainingDataset.header.rows) * s_trainingDataset.header.cols;
            const auto totalImages = static_cast<size_t>(s_trainingDataset.header.num_images);
            const auto testImageCount = std::max<size_t>(1, totalImages / 10);
            const auto trainImageCount = totalImages - testImageCount;
            const auto splitOffset = trainImageCount * imageSize;

            s_testDataset.images.assign(s_trainingDataset.images.begin() + static_cast<std::ptrdiff_t>(splitOffset),
                                        s_trainingDataset.images.end());
            s_testDataset.labels.assign(s_trainingDataset.labels.begin() + static_cast<std::ptrdiff_t>(trainImageCount),
                                        s_trainingDataset.labels.end());

            s_trainingDataset.images.resize(splitOffset);
            s_trainingDataset.labels.resize(trainImageCount);

            s_trainingDataset.header.num_images = static_cast<uint32_t>(trainImageCount);
            s_testDataset.header.num_images = static_cast<uint32_t>(testImageCount);
            s_datasetLoaded = true;
        }

        host.trainingDataset = s_trainingDataset;
        host.testDataset = s_testDataset;
    }

    void initVulkan() {
        ContextCreateInfo info{
            .settings =  {
                .queueFlags =  VK_QUEUE_COMPUTE_BIT,
            }
        };
        info.instanceExtAndLayers.extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        context = std::make_unique<VulkanContext>(info);
        context->init();
    }

    void createDescriptorPool() {
        constexpr uint32_t maxSets = 10;
        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets},
        }};

        descriptorPool = context->device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    }

    void createDeviceBuffers() {
        const auto createBuffers = [&](const auto& source) {
            return map_range(source, [&](const auto& values) {
                return context->device.createCpuVisibleBuffer(
                    values.data(),
                    BYTE_SIZE(values),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                );
            });
        };

        device.weights = createBuffers(host.weights);
        device.biases = createBuffers(host.biases);
        device.activations = createBuffers(host.activations);
        device.intermediateWeights = createBuffers(host.intermediateWeights);
        device.delta = createBuffers(host.delta);
        device.nablaBiases = createBuffers(host.nablaBiases);
        device.nablaWeights = createBuffers(host.nablaWeights);

        trainingImages = context->device.createCpuVisibleBuffer(
            host.trainingDataset.images.data(),
            BYTE_SIZE(host.trainingDataset.images),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        trainingLabels = context->device.createCpuVisibleBuffer(
            host.trainingDataset.labels.data(),
            BYTE_SIZE(host.trainingDataset.labels),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        std::vector<int> locks(host.trainingDataset.header.num_images, 0);
        trainingLocks[0] = context->device.createCpuVisibleBuffer(
            locks.data(),
            BYTE_SIZE(locks),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        trainingLocks[1] = context->device.createCpuVisibleBuffer(
            locks.data(),
            BYTE_SIZE(locks),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        testImages = context->device.createCpuVisibleBuffer(
            host.testDataset.images.data(),
            BYTE_SIZE(host.testDataset.images),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        testLabels = context->device.createCpuVisibleBuffer(
            host.testDataset.labels.data(),
            BYTE_SIZE(host.testDataset.labels),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
    }

    void createDescriptorSetLayouts() {
        datasetDescriptorSetLayout =
            context->device.descriptorSetLayoutBuilder()
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL)
                .binding(1)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL)
                .binding(2)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(trainingLocks))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .createLayout();

        neuralNetworkDescriptorSetLayout =
            context->device.descriptorSetLayoutBuilder()
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.weights))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(1)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.biases))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(2)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.activations))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(3)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.intermediateWeights))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(4)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.delta))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(5)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.nablaWeights))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .binding(6)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(COUNT(device.nablaBiases))
                    .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
                .createLayout();
    }

    void updateDescriptorSets() {
        auto sets = descriptorPool.allocate({ datasetDescriptorSetLayout, datasetDescriptorSetLayout, neuralNetworkDescriptorSetLayout });
        trainingDatasetDescriptorSet = sets[0];
        testDatasetDescriptorSet = sets[1];
        neuralNetworkDescriptorSet = sets[2];

        auto writes = initializers::writeDescriptorSets<12>();

        VkDescriptorBufferInfo trainingImagesInfo{ trainingImages, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo trainingLabelsInfo{ trainingLabels, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo testImagesInfo{ testImages, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo testLabelsInfo{ testLabels, 0, VK_WHOLE_SIZE };
        auto descriptorInfo = [](const auto& buffer) {
            return VkDescriptorBufferInfo{ buffer, 0, VK_WHOLE_SIZE };
        };

        auto trainingLocksInfo = map_range(trainingLocks, descriptorInfo);
        auto weightsInfo = map_range(device.weights, descriptorInfo);
        auto biasesInfo = map_range(device.biases, descriptorInfo);
        auto activationsInfo = map_range(device.activations, descriptorInfo);
        auto intermediateWeightsInfo = map_range(device.intermediateWeights, descriptorInfo);
        auto deltaInfo = map_range(device.delta, descriptorInfo);
        auto nablaWeightsInfo = map_range(device.nablaWeights, descriptorInfo);
        auto nablaBiasesInfo = map_range(device.nablaBiases, descriptorInfo);

        writes[0].dstSet = trainingDatasetDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &trainingImagesInfo;

        writes[1].dstSet = trainingDatasetDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &trainingLabelsInfo;

        writes[2].dstSet = trainingDatasetDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = COUNT(trainingLocksInfo);
        writes[2].pBufferInfo = trainingLocksInfo.data();

        writes[3].dstSet = testDatasetDescriptorSet;
        writes[3].dstBinding = 0;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &testImagesInfo;

        writes[4].dstSet = testDatasetDescriptorSet;
        writes[4].dstBinding = 1;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &testLabelsInfo;

        writes[5].dstSet = neuralNetworkDescriptorSet;
        writes[5].dstBinding = 0;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = COUNT(weightsInfo);
        writes[5].pBufferInfo = weightsInfo.data();

        writes[6].dstSet = neuralNetworkDescriptorSet;
        writes[6].dstBinding = 1;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = COUNT(biasesInfo);
        writes[6].pBufferInfo = biasesInfo.data();

        writes[7].dstSet = neuralNetworkDescriptorSet;
        writes[7].dstBinding = 2;
        writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[7].descriptorCount = COUNT(activationsInfo);
        writes[7].pBufferInfo = activationsInfo.data();

        writes[8].dstSet = neuralNetworkDescriptorSet;
        writes[8].dstBinding = 3;
        writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[8].descriptorCount = COUNT(intermediateWeightsInfo);
        writes[8].pBufferInfo = intermediateWeightsInfo.data();

        writes[9].dstSet = neuralNetworkDescriptorSet;
        writes[9].dstBinding = 4;
        writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[9].descriptorCount = COUNT(deltaInfo);
        writes[9].pBufferInfo = deltaInfo.data();

        writes[10].dstSet = neuralNetworkDescriptorSet;
        writes[10].dstBinding = 5;
        writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[10].descriptorCount = COUNT(nablaWeightsInfo);
        writes[10].pBufferInfo = nablaWeightsInfo.data();

        writes[11].dstSet = neuralNetworkDescriptorSet;
        writes[11].dstBinding = 6;
        writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[11].descriptorCount = COUNT(nablaBiasesInfo);
        writes[11].pBufferInfo = nablaBiasesInfo.data();

        context->device.updateDescriptorSets(writes);
    }

    void initCompute() {
        compute = ComputePipelines{
            &context->device,
            {
                {
                    "feed_forward",
                    resource("feed_forward.comp.spv"),
                    { &neuralNetworkDescriptorSetLayout, &datasetDescriptorSetLayout },
                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dev::NeuralNetwork::Constants) } }
                },
                {
                    "compute_output_activation_delta",
                    resource("compute_output_activation_delta.comp.spv"),
                    { &neuralNetworkDescriptorSetLayout, &datasetDescriptorSetLayout },
                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dev::NeuralNetwork::Constants) } }
                },
                {
                    "back_propagation",
                    resource("back_propagation.comp.spv"),
                    { &neuralNetworkDescriptorSetLayout, &datasetDescriptorSetLayout },
                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dev::NeuralNetwork::Constants) } }
                },
                {
                    "update_weights",
                    resource("update_weights.comp.spv"),
                    { &neuralNetworkDescriptorSetLayout },
                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dev::NeuralNetwork::Constants) } }
                },
                {
                    "update_biases",
                    resource("update_biases.comp.spv"),
                    { &neuralNetworkDescriptorSetLayout },
                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dev::NeuralNetwork::Constants) } }
                }
            }
        };
        compute.createPipelines();
    }

    static void updateSearchPath() {
        std::filesystem::current_path("../examples");
        spdlog::info("working directory: {}", std::filesystem::current_path().string());
        FileManager::instance().addSearchPathFront(".");
        FileManager::instance().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
        FileManager::instance().addSearchPathFront("../data");
        FileManager::instance().addSearchPathFront("neural_networks_hello_world/spv");
    }

    static auto resource(const std::string& name) {
        const auto res = FileManager::instance().getFullPath(name);
        assert(res.has_value());
        return res->string();
    }

    template<typename Func>
    void execute(Func&& func){
        context->device.computeCommandPool().oneTimeCommand(func);
    }

    static std::vector<float> dot(const std::span<float> m, const std::span<float> v) {
        assert(m.size() % v.size() == 0);
        auto rSize = m.size()/v.size();
        std::vector<float> result(rSize);

        auto cSize = v.size();
        for (auto i = 0; i < rSize; ++i) {
            auto offset = i * cSize;
            auto r = std::span{ m.data() + offset, cSize};
            float sum = 0.0f;
            for (auto j = 0; j < cSize; ++j) {
                sum += r[j] * v[j];
            }
            result[i] = sum;
        }

        return result;
    }

    static std::vector<float> dotSingle(const std::span<float> rowMatrix, const std::span<float> colMatrix) {
        assert(!rowMatrix.empty());
        assert(!colMatrix.empty());

        const auto rows = rowMatrix.size();
        const auto cols = colMatrix.size();
        std::vector<float> result(rows * cols);

        for (size_t row = 0; row < rows; ++row) {
            for (size_t col = 0; col < cols; ++col) {
                result[row * cols + col] = rowMatrix[row] * colMatrix[col];
            }
        }

        return result;
    }

    static std::vector<float> plus(const std::span<float> a, const std::span<float> b) {
        assert(a.size() == b.size());
        std::vector<float> result(a.size());
        for (auto i = 0; i < a.size(); ++i) {
            result[i] = a[i] + b[i];
        }
        return result;
    }

    static std::vector<float> minus(const std::span<float> a, const std::span<float> b) {
        assert(a.size() == b.size());
        std::vector<float> result(a.size());
        for (auto i = 0; i < a.size(); ++i) {
            result[i] = a[i] - b[i];
        }
        return result;
    }

    static std::vector<float> minus(const float s, const std::span<float> a) {
        std::vector<float> result(a.size());
        for (auto i = 0; i < a.size(); ++i) {
            result[i] = s - a[i];
        }
        return result;
    }

    static std::vector<float> multiply(const std::span<float> a, const std::span<float> b) {
        assert(a.size() == b.size());
        std::vector<float> result(a.size());
        for (auto i = 0; i < a.size(); ++i) {
            result[i] = a[i] * b[i];
        }
        return result;
    }

    static std::vector<float> sigmoid(std::span<float> Z) {
        return map_range(Z, [](auto z){ return 1.f / (1.f + std::expf(-z)); });
    }

    static std::vector<float> sigmoid_prime(std::span<float> Z) {
        auto sig = sigmoid(Z);
        auto a = minus(1.0f, sig);
        return multiply(a, sig);
    }

    static std::vector<float> cost_derivative(std::span<float> a, std::span<float>y) {
        return minus(a, y);
    }

    void feedForward() {
        std::copy_n(host.trainingDataset.images.data(), 784, host.activations[0].begin());

        const auto numLayers = host.layers.size();
        for (auto l = 0; l < numLayers - 1; ++l) {
            auto& a = host.activations[l];
            auto& b = host.biases[l];
            auto& w = host.weights[l];
            auto z = dot(w, a);
            z =  plus(z, b);
            host.intermediateWeights[l] = z;
            host.activations[l+1] = sigmoid(z);
        }
    }

    static std::vector<float> transpose(std::span<float> m, std::tuple<int, int> shape) {
        auto [rows, cols] = shape;

        assert(m.size() == rows * cols);

        std::vector<float> result(m.size());

        for (uint r = 0; r < rows; ++r) {
            for (uint c = 0; c < cols; ++c) {
                // (r, c) → (c, r)
                result[c * rows + r] = m[r * cols + c];
            }
        }

        return result;
    }

    void outputActivationDelta() {
        std::vector<float> y(host.activations.back().size(), 0.0f);
        const auto last = host.layers.size() - 1;

        auto index = host.trainingDataset.labels.front();
        y[index] = 1;
        auto Ca = cost_derivative(host.activations.back(), y);
        auto sig_prim = sigmoid_prime(host.intermediateWeights.back());

        auto delta = multiply(Ca, sig_prim);
        host.delta[last] = delta;
        host.nablaBiases[last - 1] = delta;
        auto aT = host.activations[last - 1];
        host.nablaWeights[last - 1] = dotSingle(delta, aT);
    }

    void backPropagate() {

        for (auto l = static_cast<int>(host.layers.size() - 2); l > 0; --l) {
            auto delta1 = host.delta[l+1];
            auto r = host.layers[l+1];
            auto c = host.layers[l];
            auto z = host.intermediateWeights[l-1];
            auto zp = sigmoid_prime(z);
            auto wT = transpose(host.weights[l], std::make_tuple(r, c));
            auto delta = dot(wT, delta1);
            delta = multiply(delta, zp);
            host.delta[l] = delta;
            host.nablaBiases[l-1] = delta;
            auto aT = host.activations[l-1];
            host.nablaWeights[l - 1] = dotSingle(delta, aT);
        }
    }

    void updateWeights() {
        const auto layers = host.layers.size() - 1;

        const auto n = constants.eta;
        const auto m = static_cast<float>(constants.batchSize);
        for (auto l = 0; l < layers; ++l) {
            const auto size = host.layers[l] * host.layers[l+1];
            for (auto k = 0; k < size; ++k) {
                const auto w = host.weights[l][k];
                const auto nw = host.nablaWeights[l][k];

                host.weights[l][k] = w - (n/m) * nw;
            }
        }
    }

    void updateBiases() {
        const auto layers = host.layers.size() - 1;

        const auto n = constants.eta;
        const auto m = static_cast<float>(constants.batchSize);
        for (auto l = 0; l < layers; ++l) {
            const auto size = host.layers[l+1];
            for (auto j = 0; j < size; ++j) {
                const auto b = host.biases[l][j];
                const auto nb = host.nablaBiases[l][j];

                host.biases[l][j] = b - (n/m) * nb;
            }
        }
    }

    void updateWeightsAndBiases() {
        updateWeights();
        updateBiases();
    }

    static void copy(VkCommandBuffer commandBuffer, const VulkanBuffer &source, const VulkanBuffer &destination) {
        assert(source);
        assert(destination);

        VkBufferCopy region{};
        region.size = std::min(source.size, destination.size);
        vkCmdCopyBuffer(commandBuffer, source, destination, 1, &region);
        Barrier::transferWriteToComputeRead(commandBuffer);
    }

    void feedForward(VkCommandBuffer commandBuffer) const {
        const auto gx = host.layers[constants.layerIndex + 1];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("feed_forward"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("feed_forward"), 0, 1, &neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("feed_forward"), 1, 1, &trainingDatasetDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, compute.layout("feed_forward"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    }

    void loadTrainingInput(VkCommandBuffer commandBuffer) const {
        copy(commandBuffer, trainingImages, device.activations[0]);
    }

    void feedForwardAll(VkCommandBuffer commandBuffer) {
        constants.batchSize = 100;
        constants.numLayers = 3;
        auto numLayers = host.layers.size();

        for (auto layer = 0; layer < numLayers - 1; ++layer) {
            constants.layerIndex = layer;
            feedForward(commandBuffer);
            Barrier::computeWriteToRead(commandBuffer);
        }
    }

    void computeOutputActivationDelta(VkCommandBuffer commandBuffer) const {
        const auto gx = host.layers[constants.layerIndex + 1];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("compute_output_activation_delta"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("compute_output_activation_delta"), 0, 1, &neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("compute_output_activation_delta"), 1, 1, &trainingDatasetDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, compute.layout("compute_output_activation_delta"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    }

    void computeBackPropagation(VkCommandBuffer commandBuffer) const {
        const auto gx = host.layers[constants.layerIndex];
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("back_propagation"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("back_propagation"), 0, 1, &neuralNetworkDescriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, compute.layout("back_propagation"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    }

};

// Demonstrate some basic assertions.

TEST_F(NeuralNetworkFixture, computeIntermediateWeight) {

    std::copy_n(host.trainingDataset.images.begin(), 784, host.activations[0].begin());

    const auto numLayers = host.layers.size();
    constexpr auto layer = 0;
    auto& a = host.activations[layer];
    auto& b = host.biases[layer];
    auto& w = host.weights[layer];
    auto z = dot(w, a);
    z =  plus(z, b);
    host.intermediateWeights[layer] = z;
    host.activations[layer+1] = sigmoid(z);

    execute([&](auto commandBuffer) {
        std::copy_n(host.layers.begin(), numLayers, constants.layers.begin());
        constants.layerIndex = layer;

        loadTrainingInput(commandBuffer);
        feedForward(commandBuffer);
    });

    const auto dev_z = device.intermediateWeights[layer].span<float>();
    const auto host_z = host.intermediateWeights[layer];

    constexpr auto nextLayer = layer + 1;
    for (auto i = 0; i < host.layers[nextLayer]; ++i) {
        EXPECT_NEAR(host_z[i], dev_z[i], 1e-3) << "neuron " << i << " => " << dev_z[i] << " != " << host_z[i];
    }

    const auto dev_a1 = device.activations[nextLayer].span<float>();
    const auto host_a1 = host.activations[nextLayer];

    for (auto i = 0; i < host.layers[nextLayer]; ++i) {
        EXPECT_NEAR(host_a1[i], dev_a1[i], 1e-3);
    }

}

TEST_F(NeuralNetworkFixture, feedForwardFunction) {

    feedForward();
    // execute([&](auto commandBuffer) {
    //     loadTrainingInput(commandBuffer);
    //     feedForwardAll(commandBuffer);
    // });

    execute([&](auto commandBuffer) {
        network.loadInputLayer(commandBuffer);
        network.feedForward(commandBuffer);
    });
    
    const auto numLayers = host.layers.size();
    for (auto i = 0; i < numLayers; ++i) {
        auto dev_a = network.m_activations[i].span<float>();
        auto host_a = host.activations[i];

        for (auto j = 0; j < host_a.size(); ++j) {
            ASSERT_NEAR(host_a[j], dev_a[j], 1E-3) << fmt::format("activation[{}] in layer: {} does not match {} != {}", j,  i, host_a[j], dev_a[j]);
        }
    }
}


TEST_F(NeuralNetworkFixture, outputActivationDelta) {
    feedForward();
    outputActivationDelta();

    const auto last = host.layers.size() - 1;
    const auto outputLayer = last - 1;

    // execute([&](auto commandBuffer) {
    //     loadTrainingInput(commandBuffer);
    //     feedForwardAll(commandBuffer);
    //     constants.layerIndex = static_cast<uint>(outputLayer);
    //     computeOutputActivationDelta(commandBuffer);
    //     Barrier::computeWriteToRead(commandBuffer);
    // });
    execute([&](auto commandBuffer) {
        network.loadInputLayer(commandBuffer);
        network.feedForward(commandBuffer);
        network.computeOutputActivationDelta(commandBuffer);
    });

    const auto dev_delta = network.m_delta[last].span<float>();
    const auto host_delta = host.delta[last];
    for (auto i = 0; i < host_delta.size(); ++i) {
        ASSERT_NEAR(host_delta[i], dev_delta[i], 1E-3) << fmt::format("delta[{}] mismatch {} != {}", i, host_delta[i], dev_delta[i]);
    }

    const auto dev_nablaBias = network.m_nablaBiases[outputLayer].span<float>();
    const auto host_nablaBias = host.nablaBiases[outputLayer];
    for (auto i = 0; i < host_nablaBias.size(); ++i) {
        ASSERT_NEAR(host_nablaBias[i], dev_nablaBias[i], 1E-3) << fmt::format("nablaBias[{}] mismatch {} != {}", i, host_nablaBias[i], dev_nablaBias[i]);
    }

    const auto dev_nablaWeights = network.m_nablaWeights[outputLayer].span<float>();
    const auto host_nablaWeights = host.nablaWeights[outputLayer];
    for (auto i = 0; i < host_nablaWeights.size(); ++i) {
        ASSERT_NEAR(host_nablaWeights[i], dev_nablaWeights[i], 1E-3) << fmt::format("nablaWeight[{}] mismatch {} != {}", i, host_nablaWeights[i], dev_nablaWeights[i]);
    }
}

TEST_F(NeuralNetworkFixture, backPropagation) {
    feedForward();
    outputActivationDelta();
    backPropagate();

    // execute([&](auto commandBuffer) {
    //     loadTrainingInput(commandBuffer);
    //     feedForwardAll(commandBuffer);
    //     constants.layerIndex = static_cast<uint>(host.layers.size() - 2);
    //     computeOutputActivationDelta(commandBuffer);
    //     Barrier::computeWriteToRead(commandBuffer);
    //
    //     for (auto layer = static_cast<int>(host.layers.size() - 2); layer > 0; --layer) {
    //         constants.layerIndex = static_cast<uint>(layer);
    //         computeBackPropagation(commandBuffer);
    //         Barrier::computeWriteToRead(commandBuffer);
    //     }
    // });

    execute([&](auto commandBuffer) {
        network.loadInputLayer(commandBuffer);
        network.feedForward(commandBuffer);
        network.computeOutputActivationDelta(commandBuffer);
        network.computeBackPropagation(commandBuffer);
    });

    for (size_t hiddenLayer = 1; hiddenLayer + 1 < host.layers.size(); ++hiddenLayer) {
        const auto hiddenParamLayer = hiddenLayer - 1;

        const auto dev_delta = network.m_delta[hiddenLayer].span<float>();
        const auto& host_delta = host.delta[hiddenLayer];
        for (size_t i = 0; i < host_delta.size(); ++i) {
            ASSERT_NEAR(host_delta[i], dev_delta[i], 1E-3)
                << fmt::format("hidden layer {} delta[{}] mismatch {} != {}", hiddenLayer, i, host_delta[i], dev_delta[i]);
        }

        const auto dev_nablaBias = network.m_nablaBiases[hiddenParamLayer].span<float>();
        const auto& host_nablaBias = host.nablaBiases[hiddenParamLayer];
        for (size_t i = 0; i < host_nablaBias.size(); ++i) {
            ASSERT_NEAR(host_nablaBias[i], dev_nablaBias[i], 1E-3)
                << fmt::format("hidden layer {} nablaBias[{}] mismatch {} != {}", hiddenLayer, i, host_nablaBias[i], dev_nablaBias[i]);
        }

        const auto dev_nablaWeights = network.m_nablaWeights[hiddenParamLayer].span<float>();
        const auto& host_nablaWeights = host.nablaWeights[hiddenParamLayer];
        for (size_t i = 0; i < host_nablaWeights.size(); ++i) {
            ASSERT_NEAR(host_nablaWeights[i], dev_nablaWeights[i], 1E-3)
                << fmt::format("hidden layer {} nablaWeight[{}] mismatch {} != {}", hiddenLayer, i, host_nablaWeights[i], dev_nablaWeights[i]);
        }
    }
}

TEST_F(NeuralNetworkFixture, weightsAndBiasesUpdate) {
    feedForward();
    outputActivationDelta();
    backPropagate();
    updateWeightsAndBiases();

    execute([&](auto commandBuffer) {
        network.loadInputLayer(commandBuffer);
        network.feedForward(commandBuffer);
        network.computeOutputActivationDelta(commandBuffer);
        network.computeBackPropagation(commandBuffer);
        network.updateWeightsAndBiases(commandBuffer);
    });

    for (size_t layer = 0; layer < host.weights.size(); ++layer) {
        const auto dev_weights = network.m_weights[layer].span<float>();
        const auto& host_weights = host.weights[layer];
        for (size_t i = 0; i < host_weights.size(); ++i) {
            ASSERT_NEAR(host_weights[i], dev_weights[i], 1E-3)
                << fmt::format("weights layer {} [{}] mismatch {} != {}", layer, i, host_weights[i], dev_weights[i]);
        }
    }

    for (size_t layer = 0; layer < host.biases.size(); ++layer) {
        const auto dev_biases = network.m_biases[layer].span<float>();
        const auto& host_biases = host.biases[layer];
        for (size_t i = 0; i < host_biases.size(); ++i) {
            ASSERT_NEAR(host_biases[i], dev_biases[i], 1E-3)
                << fmt::format("biases layer {} [{}] mismatch {} != {}", layer, i, host_biases[i], dev_biases[i]);
        }
    }
}

TEST_F(NeuralNetworkFixture, playground) {
    mnist::Dataset trainingData{};
    trainingData.header = host.trainingDataset.header;
    trainingData.header.num_images = 1;
    trainingData.images.resize(784);
    trainingData.labels.resize(1);
    std::copy_n(host.trainingDataset.images.begin(), 784, trainingData.images.begin());
    std::copy_n(host.trainingDataset.labels.begin(), 1, trainingData.labels.begin());

    auto data = to_matrix(trainingData);

    auto [m, l] = data.front();

    cpu::NeuralNetwork cpuNetwork{{784, 30, 10}, true};
    cpuNetwork.train(data, 1, 1, constants.eta);

    feedForward();
    outputActivationDelta();
    backPropagate();
    updateWeightsAndBiases();


    for (size_t layer = 0; layer < host.weights.size(); ++layer) {
        const auto dev_weights = cpuNetwork.m_weights[layer];
        const auto& host_weights = host.weights[layer];

        nda::for_all_indices(dev_weights.shape(), [&](auto i, auto j) {
            auto index = i * dev_weights.j().extent() + j;
            EXPECT_NEAR(host_weights[index], dev_weights(i, j), 1E-3)
                << fmt::format("weights layer {} [{}] mismatch {} != {}, diff = {}", layer, i, host_weights[index], dev_weights(i, j), std::abs(host_weights[index] - dev_weights(i, j)));
        });
    }

    for (size_t layer = 0; layer < host.biases.size(); ++layer) {
        const auto dev_biases = cpuNetwork.m_biases[layer];
        const auto& host_biases = host.biases[layer];

        nda:;nda::for_all_indices(dev_biases.shape(), [&](auto i, auto j) {
            auto index = i * dev_biases.j().extent() + j;
            EXPECT_NEAR(host_biases[index], dev_biases(i, j), 1E-3)
               << fmt::format("biases layer {} [{}] mismatch {} != {}, diff = {}", layer, i, host_biases[index], dev_biases(i, j), std::abs(host_biases[index] - dev_biases(i, j)));
        });
    }

    ASSERT_TRUE(true);

}

TEST_F(NeuralNetworkFixture, DISABLED_cppNetTest) {
    auto samples = host.trainingDataset.header.num_images;
    mnist::Dataset trainingData{};
    trainingData.header = host.trainingDataset.header;
    trainingData.header.num_images = samples;
    trainingData.images.resize(784 * samples);
    trainingData.labels.resize(1 * samples);
    std::copy_n(host.trainingDataset.images.begin(), 784 * samples, trainingData.images.begin());
    std::copy_n(host.trainingDataset.labels.begin(), 1 * samples, trainingData.labels.begin());

    samples = host.testDataset.header.num_images;
    mnist::Dataset testdata{};
    testdata.header = host.testDataset.header;
    testdata.header.num_images = samples;
    testdata.images.resize(784 * samples);
    testdata.labels.resize(1 * samples);
    std::copy_n(host.testDataset.images.begin(), 784 * samples, testdata.images.begin());
    std::copy_n(host.testDataset.labels.begin(), 1 * samples, testdata.labels.begin());

    auto data = to_matrix(trainingData);
    auto tData = to_matrix(testdata);
    cpu::NeuralNetwork cpuNetwork{{784, 30, 10}, true};
    cpuNetwork.train(data, 30, 10, 3.0, tData);
}

mnist::Dataset NeuralNetworkFixture::s_trainingDataset{};
mnist::Dataset NeuralNetworkFixture::s_testDataset{};
bool NeuralNetworkFixture::s_datasetLoaded = false;
