#include "NeuralNetworksDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Vertex.h"
#include "Barrier.hpp"

#include "mnist/mnist_loader.hpp"
#include "cpu/NeuralNetwork.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {
    constexpr uint32_t kWeightsBiasesMagic = 0x4E4E5742;
    constexpr uint32_t kWeightsBiasesVersion = 1;

    std::vector<float> readMatrix(std::ifstream& in) {
        uint32_t rows{};
        uint32_t cols{};
        in.read(reinterpret_cast<char*>(&rows), sizeof(rows));
        in.read(reinterpret_cast<char*>(&cols), sizeof(cols));

        std::vector<float> values(static_cast<size_t>(rows) * cols);
        in.read(reinterpret_cast<char*>(values.data()), BYTE_SIZE(values));
        return values;
    }

    std::tuple<std::vector<std::vector<float>>, std::vector<std::vector<float>>> loadWeightsAndBiases(const std::filesystem::path& path) {
        std::ifstream in{path, std::ios::binary};
        if (!in.is_open()) {
            spdlog::warn("weights/biases file not found: {}", path.string());
            return {};
        }

        uint32_t magic{};
        uint32_t version{};
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (magic != kWeightsBiasesMagic || version != kWeightsBiasesVersion) {
            spdlog::warn("ignoring invalid weights/biases file: {}", path.string());
            return {};
        }

        uint32_t weightsCount{};
        in.read(reinterpret_cast<char*>(&weightsCount), sizeof(weightsCount));
        std::vector<std::vector<float>> weights;
        weights.reserve(weightsCount);
        for (uint32_t i = 0; i < weightsCount; ++i) {
            weights.push_back(readMatrix(in));
        }

        uint32_t biasesCount{};
        in.read(reinterpret_cast<char*>(&biasesCount), sizeof(biasesCount));
        std::vector<std::vector<float>> biases;
        biases.reserve(biasesCount);
        for (uint32_t i = 0; i < biasesCount; ++i) {
            biases.push_back(readMatrix(in));
        }

        spdlog::info("loaded {} weight layers and {} bias layers from {}", weights.size(), biases.size(), path.string());
        return {std::move(weights), std::move(biases)};
    }
}


NeuralNetworksDemo::NeuralNetworksDemo(const Settings& settings) : VulkanBaseApp("Neural Networks Hello World", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("neural_networks_hello_world");
    fileManager().addSearchPathFront("neural_networks_hello_world/data");
    fileManager().addSearchPathFront("neural_networks_hello_world/spv");
    fileManager().addSearchPathFront("neural_networks_hello_world/models");
    fileManager().addSearchPathFront("neural_networks_hello_world/textures");
}

void NeuralNetworksDemo::initApp() {
    loadDataset();
    fullscreenQuad = device.createDeviceLocalBuffer(
        ClipSpace::Quad::positions.data(),
        BYTE_SIZE(ClipSpace::Quad::positions),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    );
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
    initNetwork();
}

void NeuralNetworksDemo::loadDataset() {
    auto testDataset = mnist::load(resource("mnist_dataset/t10k-images.idx3-ubyte"),
                                        resource("mnist_dataset/t10k-labels.idx1-ubyte"));
    auto trainingDataset = mnist::load(resource("mnist_dataset/train-images.idx3-ubyte"),
                                        resource("mnist_dataset/train-labels.idx1-ubyte"));
    trainingSetHeader = trainingDataset.header;
    testSetHeader = testDataset.header;
    constants.width = testDataset.header.cols;
    constants.height = testDataset.header.rows;
    constants.imageCount = trainingDataset.header.num_images;
    constants.offset = 0;
    std::vector<int> locks(trainingDataset.header.num_images, 0);

    trainingImages = device.createDeviceLocalBuffer(trainingDataset.images.data(), BYTE_SIZE(trainingDataset.images), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    trainingLabels = device.createDeviceLocalBuffer(trainingDataset.labels.data(), BYTE_SIZE(trainingDataset.labels), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    trainingLocks[0] = device.createDeviceLocalBuffer(locks.data(), BYTE_SIZE(locks), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    trainingLocks[1] = device.createDeviceLocalBuffer(locks.data(), BYTE_SIZE(locks), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );

    testImages = device.createDeviceLocalBuffer(testDataset.images.data(), BYTE_SIZE(testDataset.images), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    testLabels = device.createDeviceLocalBuffer(testDataset.labels.data(), BYTE_SIZE(testDataset.labels), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    results = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, BYTE_SIZE(testDataset.labels), "result_buffer");

    inputBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, 28 * 28 * sizeof(float), "input_buffer");
    canvasBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, 280 * 280 * sizeof(float), "canvas_buffer");

    textures::createNoTransition(device, inputTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, {28, 28, 1});
    inputTexture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inputTextureID = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(inputTexture.imageView);
    spdlog::info("MNIST dataset loaded");
}

void NeuralNetworksDemo::initNetwork() {
    auto [weights, biases] = loadWeightsAndBiases("neural_networks_hello_world/data/cpu_weights_biases.bin");
    network = dev::NeuralNetwork{ &device, datasetDescriptorSetLayout, {784, 30, 10},  {
        .trainingData = std::make_tuple(trainingDatasetDescriptorSet, dev::NeuralNetwork::Dataset{ trainingImages, trainingLabels }),
        .epochs = 10,
        .numBatches = 6000,
        .datasetSize = 60000,
        .eta = 3.0f,
        .testData = std::make_tuple(testDatasetDescriptorSet, dev::NeuralNetwork::Dataset{ testImages, testLabels }),
    // }};
    }, std::move(weights), std::move(biases)};
    network.init();
    output = network.m_activations[2].span<float>(10);
     // device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
     //     network.train(commandBuffer);
     // });
}

void NeuralNetworksDemo::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void NeuralNetworksDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void NeuralNetworksDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void NeuralNetworksDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 4> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}


void NeuralNetworksDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void NeuralNetworksDemo::createDescriptorSetLayouts() {
    datasetDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
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
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    canvasDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();

    canvasToInputDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void NeuralNetworksDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({
        datasetDescriptorSetLayout,
        datasetDescriptorSetLayout,
        canvasDescriptorSetLayout,
        canvasToInputDescriptorSetLayout
    });
    trainingDatasetDescriptorSet = sets[0];
    testDatasetDescriptorSet = sets[1];
    canvasDescriptorSet = sets[2];
    canvasToInputDescriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<10>();

    VkDescriptorBufferInfo trainingImagesInfo{ trainingImages, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo trainingLabelsInfo{ trainingLabels, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo testImagesInfo{ testImages, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo testLabelsInfo{ testLabels, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo resultsInfo{ results, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo canvasInfo{ canvasBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo inputInfo{ inputBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorImageInfo inputImageInfo{ nullptr, inputTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    auto descriptorInfo = [](const auto& buffer) {
        return VkDescriptorBufferInfo{ buffer, 0, VK_WHOLE_SIZE };
    };
    auto trainingLocksInfo = map_range(trainingLocks, descriptorInfo);

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

    writes[5].dstSet = testDatasetDescriptorSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &resultsInfo;

    writes[6].dstSet = canvasDescriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &canvasInfo;

    writes[7].dstSet = canvasToInputDescriptorSet;
    writes[7].dstBinding = 0;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    writes[7].pBufferInfo = &canvasInfo;

    writes[8].dstSet = canvasToInputDescriptorSet;
    writes[8].dstBinding = 1;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    writes[8].pBufferInfo = &inputInfo;

    writes[9].dstSet = canvasToInputDescriptorSet;
    writes[9].dstBinding = 2;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[9].descriptorCount = 1;
    writes[9].pImageInfo = &inputImageInfo;

    device.updateDescriptorSets(writes);
}

void NeuralNetworksDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void NeuralNetworksDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void NeuralNetworksDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .layout()
                    .clear()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants))
                    .addDescriptorSetLayout(datasetDescriptorSetLayout)
                    .addDescriptorSetLayout(datasetDescriptorSetLayout)
                    .addDescriptorSetLayout(canvasToInputDescriptorSetLayout)
                .vertexInputState()
                    .clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                .depthStencilState()
                    .disableDepthTest()
                    .disableDepthWrite()
                .shaderStage()
                    .vertexShader(resource("fullscreen_uv.vert.spv"))
                    .fragmentShader(resource("fullscreen_uv.frag.spv"))
                .name("render")
                .build(render.layout);

        canvas.pipeline =
            prototypes->cloneGraphicsPipeline()
                .layout()
                    .clear()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(CanvasConstants))
                    .addDescriptorSetLayout(canvasDescriptorSetLayout)
                .vertexInputState()
                    .clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                    .frontFaceCounterClockwise()
                .depthStencilState()
                    .disableDepthTest()
                    .disableDepthWrite()
                .shaderStage()
                    .vertexShader(resource("fullscreen_uv.vert.spv"))
                    .fragmentShader(resource("canvas.frag.spv"))
                .name("canvas")
                .build(canvas.layout);
    //    @formatter:on
}

void NeuralNetworksDemo::createComputePipelines() {
    computePipelines = ComputePipelines{&device, {
        {
            "canvas_to_input",
            resource("canvas_to_input.comp.spv"),
            { &canvasToInputDescriptorSetLayout },
            { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CanvasToInputConstants) } }
        }
    }};
    computePipelines.createPipelines();
}


void NeuralNetworksDemo::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(canvas.pipeline);
}

void NeuralNetworksDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *NeuralNetworksDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        // renderCanvas(commandBuffer);
        renderTrainingData(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    canvasToInput(commandBuffer);
    if (shouldEvaluate) {
        network.evaluate(commandBuffer, inputBuffer);
    }

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void NeuralNetworksDemo::renderCanvas(VkCommandBuffer commandBuffer) {
    VkDeviceSize vertexOffset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, canvas.pipeline.handle);
    vkCmdPushConstants(commandBuffer, canvas.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(canvasConstants), &canvasConstants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, canvas.layout.handle, 0, 1, &canvasDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, fullscreenQuad, &vertexOffset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    canvasConstants.clear = 0;
}

void NeuralNetworksDemo::renderTrainingData(VkCommandBuffer commandBuffer) {
    std::array<VkDescriptorSet, 3> sets{ trainingDatasetDescriptorSet, testDatasetDescriptorSet, canvasToInputDescriptorSet };
    VkDeviceSize vertexOffset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, fullscreenQuad, &vertexOffset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    constants.mouseClicked = 0;
}

void NeuralNetworksDemo::canvasToInput(VkCommandBuffer commandBuffer) {
    constexpr uint32_t localSize = 32;
    Barrier::fragmentWriteToComputeRead(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelines.pipeline("canvas_to_input"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipelines.layout("canvas_to_input"), 0, 1, &canvasToInputDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, computePipelines.layout("canvas_to_input"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(canvasToInputConstants), &canvasToInputConstants);
    vkCmdDispatch(
        commandBuffer,
        nearestMultiple(canvasToInputConstants.inputSize.x, localSize) / localSize,
        nearestMultiple(canvasToInputConstants.inputSize.y, localSize) / localSize,
        1
    );
    Barrier::computeWriteToRead(commandBuffer);
}

void NeuralNetworksDemo::renderUI(VkCommandBuffer commandBuffer) {
    int imageOffset = static_cast<int>(constants.offset);
    const int maxOffset = constants.imageCount > 100 ? static_cast<int>(constants.imageCount - 100) : 0;

    ImGui::Begin("Dataset");
    ImGui::SetWindowSize({0, 0});
    ImGui::Image(inputTextureID, {128, 128});
    if (ImGui::SliderInt("Offset", &imageOffset, 0, maxOffset)) {
        constants.offset = static_cast<uint32_t>(imageOffset);
    }
    if (ImGui::Button("Shuffle")) {
        network.shuffleTrainingData(commandBuffer);
        Barrier::computeWriteToFragmentRead(commandBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("evaluate")) {
        shouldEvaluate = true;
    }
    ImGui::Text("Showing images %u - %u", constants.offset, std::min(constants.offset + 99u, constants.imageCount ? constants.imageCount - 1 : 0u));
    ImGui::End();

    // ImGui::Begin("Brush");
    // ImGui::SetWindowSize({0, 0});
    // ImGui::Image(inputTextureID, {128, 128});
    // ImGui::SliderFloat("radius", &canvasConstants.radius, 0.001f, 0.1f);
    //
    // if (ImGui::Button("clear")) {
    //     canvasConstants.clear = true;
    // }
    // if (ImGui::Button("evaluate")) {
    //     shouldEvaluate = true;
    // }
    // ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void NeuralNetworksDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();

}

void NeuralNetworksDemo::checkAppInputs() {
    camera->processInput();

    const auto& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        canvasConstants.active = 0;
        return;
    }


    if (mouse.left.held){
        canvasConstants.active = 1;
        constants.mouseClicked = 1;
    }else {
       canvasConstants.active = 0;
    }

    if (canvasConstants.active) {
        canvasConstants.center = mouse.position/glm::vec2(width, height);
    }

    if (constants.mouseClicked == 1) {
        constants.mousePos = mouse.position/glm::vec2(width, height);
    }

}

void NeuralNetworksDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void NeuralNetworksDemo::onPause() {
    VulkanBaseApp::onPause();
}

void NeuralNetworksDemo::newFrame() {
    if (shouldEvaluate) {
        auto result = argmax(output);
        auto neurons = map_range(output, [](auto n){ return int(n > 0.5); });
        spdlog::info("Network result: {}, {}", result, neurons);
        shouldEvaluate = false;
    }
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1000;
        settings.height = 1000;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = NeuralNetworksDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }

}
