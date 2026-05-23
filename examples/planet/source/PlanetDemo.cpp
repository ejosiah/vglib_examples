#include "PlanetDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

PlanetDemo::PlanetDemo(const Settings& settings) : VulkanBaseApp("Planet", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("planet");
    fileManager().addSearchPathFront("planet/data");
    fileManager().addSearchPathFront("planet/spv");
    fileManager().addSearchPathFront("planet/models");
    fileManager().addSearchPathFront("planet/textures");
}

void PlanetDemo::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initGeometry();
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void PlanetDemo::initGeometry() {
    // Num elements the CBT holds
    const auto cbtNumElements = cbt_large::cbt_num_elements(m_CBTType);
    cbt_large::CBT* cbt = create_cbt(m_CBTType);
    planetMesh = CPUMesh::load_cpu_mesh(resource("icosahedron.ccm"), cbtNumElements);

    m_EarthPlanet = Planet{device, "earth", g_EarthRadius, g_EarthCenter, g_EarthImpostorToggle, g_EarthTriangleSize, EARTH_MATERIAL};
    m_EarthPlanet.initialize(*cbt, planetMesh);

    m_MoonPlanet = Planet{device, "moon", g_MoonRadius, g_MoonCenter, g_MoonImpostorToggle, g_MoonTriangleSize, MOON_MATERIAL};
    m_MoonPlanet.initialize(*cbt, planetMesh);

    delete cbt;
}

void PlanetDemo::initCamera() {
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

void PlanetDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void PlanetDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void PlanetDemo::createDescriptorPool() {
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


void PlanetDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void PlanetDemo::createDescriptorSetLayouts() {
}

void PlanetDemo::updateDescriptorSets(){
}

void PlanetDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void PlanetDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void PlanetDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .polygonModeLine()
                    .cullNone()
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void PlanetDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void PlanetDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *PlanetDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        camera->push(commandBuffer, render.layout);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_EarthPlanet.m_BaseMesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PlanetDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void PlanetDemo::checkAppInputs() {
    camera->processInput();
}

void PlanetDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void PlanetDemo::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
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
        auto app = PlanetDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}