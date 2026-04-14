#include "NeuralNetworksDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Vertex.h"

#include "mnist/mnist_loader.hpp"
#include "cpu/NeuralNetwork.hpp"


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
    constants.imageCount = testDataset.header.num_images;
    constants.offset = 0;

    trainingImages = device.createDeviceLocalBuffer(trainingDataset.images.data(), BYTE_SIZE(trainingDataset.images), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    trainingLabels = device.createDeviceLocalBuffer(trainingDataset.labels.data(), BYTE_SIZE(trainingDataset.labels), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );

    testImages = device.createDeviceLocalBuffer(testDataset.images.data(), BYTE_SIZE(testDataset.images), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );
    testLabels = device.createDeviceLocalBuffer(testDataset.labels.data(), BYTE_SIZE(testDataset.labels), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT );

    spdlog::info("MNIST dataset loaded");
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
            .createLayout();
}

void NeuralNetworksDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ datasetDescriptorSetLayout, datasetDescriptorSetLayout });
    trainingDatasetDescriptorSet = sets[0];
    testDatasetDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<4>();

    VkDescriptorBufferInfo trainingImagesInfo{ trainingImages, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo trainingLabelsInfo{ trainingLabels, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo testImagesInfo{ testImages, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo testLabelsInfo{ testLabels, 0, VK_WHOLE_SIZE };

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

    writes[2].dstSet = testDatasetDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &testImagesInfo;

    writes[3].dstSet = testDatasetDescriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &testLabelsInfo;

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
    //    @formatter:on
}


void NeuralNetworksDemo::onSwapChainDispose() {
    dispose(render.pipeline);
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
        std::array<VkDescriptorSet, 2> sets{ trainingDatasetDescriptorSet, testDatasetDescriptorSet };
        int imageOffset = static_cast<int>(constants.offset);
        const int maxOffset = constants.imageCount > 100 ? static_cast<int>(constants.imageCount - 100) : 0;

        ImGui::Begin("Dataset");
        ImGui::SetWindowSize({300, 110});
        if (ImGui::SliderInt("Offset", &imageOffset, 0, maxOffset)) {
            constants.offset = static_cast<uint32_t>(imageOffset);
        }
        ImGui::Text("Showing images %u - %u", constants.offset, std::min(constants.offset + 99u, constants.imageCount ? constants.imageCount - 1 : 0u));
        ImGui::End();

        VkDeviceSize vertexOffset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), &constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, fullscreenQuad, &vertexOffset);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);
        plugin(IM_GUI_PLUGIN).draw(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void NeuralNetworksDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void NeuralNetworksDemo::checkAppInputs() {
    camera->processInput();
}

void NeuralNetworksDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void NeuralNetworksDemo::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    // try{
    fs::current_path("../../../../examples/");
    //     Settings settings;
    //     settings.width = 1440;
    //     settings.height = 1280;
    //     settings.depthTest = true;
    //     settings.enabledFeatures.wideLines = true;
    //     settings.enableBindlessDescriptors = true;
    //     settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    //     settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
    //     settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
    //     settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
    //     settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
    //     settings.enabledFeatures.multiDrawIndirect = VK_TRUE;
    //
    //     std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
    //     auto app = NeuralNetworksDemo{ settings };
    //     app.addPlugin(imGui);
    //     app.run();
    // }catch(std::runtime_error& err){
    //     spdlog::error(err.what());
    // }

    FileManager::instance().addSearchPathFront("../data");
    FileManager::instance().addSearchPathFront("../data/textures");
    FileManager::instance().addSearchPathFront("../data/shaders");
    FileManager::instance().addSearchPathFront("../data/models");

    auto testDataset = mnist::load(FileManager::resource("mnist_dataset/t10k-images.idx3-ubyte"),
                                    FileManager::resource("mnist_dataset/t10k-labels.idx1-ubyte"));
    auto trainingDataset = mnist::load(FileManager::resource("mnist_dataset/train-images.idx3-ubyte"),
                                        FileManager::resource("mnist_dataset/train-labels.idx1-ubyte"));

    auto samples = trainingDataset.header.num_images;
    mnist::Dataset trainingData{};
    trainingData.header = trainingDataset.header;
    trainingData.header.num_images = samples;
    trainingData.images.resize(784 * samples);
    trainingData.labels.resize(1 * samples);
    std::copy_n(trainingDataset.images.begin(), 784 * samples, trainingData.images.begin());
    std::copy_n(trainingDataset.labels.begin(), 1 * samples, trainingData.labels.begin());

    samples = testDataset.header.num_images;
    mnist::Dataset testdata{};
    testdata.header = trainingDataset.header;
    testdata.header.num_images = samples;
    testdata.images.resize(784 * samples);
    testdata.labels.resize(1 * samples);
    std::copy_n(trainingDataset.images.begin(), 784 * samples, testdata.images.begin());
    std::copy_n(trainingDataset.labels.begin(), 1 * samples, testdata.labels.begin());

    auto data = to_matrix(trainingData);
    auto tData = to_matrix(testdata);
    cpu::NeuralNetwork cpuNetwork{{784, 30, 10}, true};
    cpuNetwork.train(data, 30, 10, 3.0, tData);
}
