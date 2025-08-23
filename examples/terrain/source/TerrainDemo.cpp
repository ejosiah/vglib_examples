#include "TerrainDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

TerrainDemo::TerrainDemo(const Settings& settings) : VulkanBaseApp("Terrain", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("terrain");
    fileManager().addSearchPathFront("terrain/data");
    fileManager().addSearchPathFront("terrain/spv");
    fileManager().addSearchPathFront("terrain/models");
    fileManager().addSearchPathFront("terrain/textures");
}

void TerrainDemo::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initContext();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initTerrain();
}

void TerrainDemo::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.zFar = 64000;
//    cameraSettings.acceleration = glm::vec3(60);
//    cameraSettings.velocity = glm::vec3(120);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->position({1000.0f, 2000.5f, 1000.0f});
    camera->position({0, 1, 0});
}

void TerrainDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void TerrainDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void TerrainDemo::createDescriptorPool() {
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


void TerrainDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void TerrainDemo::createDescriptorSetLayouts() {
}

void TerrainDemo::updateDescriptorSets(){
}

void TerrainDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void TerrainDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void TerrainDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("pass_through.vert.spv"))
                    .fragmentShader(resource("pass_through.frag.spv"))
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void TerrainDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void TerrainDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *TerrainDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    terrain->preProcess(commandBuffer);

    clearColor(0, 0, 1);
    renderToSwapChain([&]{
        terrain->render(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void TerrainDemo::update(float time) {
    camera->update(time);

    setTitle(fmt::format("{}, nodes - {}, FPS - {}", title, terrain->nodeCount(), framePerSecond));
}

void TerrainDemo::checkAppInputs() {
    camera->processInput();
}

void TerrainDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void TerrainDemo::onPause() {
    VulkanBaseApp::onPause();
}

void TerrainDemo::initContext() {
    context.screenWidth = swapChain.width();
    context.screenHeight = swapChain.height();
    context.device = &device;
    context.descriptorPool = &descriptorPool;
    context.camera = camera.get();
    context.gBuffer = &gBuffer;
    context.bindlessDescriptor = &bindlessDescriptor;
    context.prototypes = std::make_unique<Prototypes>(device, swapChain, renderPass);
}

void TerrainDemo::initTerrain() {
    terrain = std::make_unique<Terrain>(context);
    terrain->init();
}


void TerrainDemo::endFrame() {
    terrain->endFrame();
}

void TerrainDemo::newFrame() {
    terrain->newFrame();
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.enabledFeatures.geometryShader = true;
        settings.enabledFeatures.tessellationShader = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = TerrainDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}