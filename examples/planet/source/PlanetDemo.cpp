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
    loadTextures();
    creatSkyBox();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createBuffers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initGeometry();
    initLoader();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void PlanetDemo::createBuffers() {
    globalBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(GlobalCB), "global_buffer");
    global = static_cast<GlobalCB *>(globalBuffer.map());
    *global = GlobalCB{};
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

    m_MeshUpdater = MeshUpdater{device, globalDescriptorSetLayout};
    m_MeshUpdater.initialize();

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

void PlanetDemo::loadTextures() {
    textures::fromFile(device, milkyway, resource("milky_way/milky_way.png"), false, VK_FORMAT_R8G8B8A8_SRGB);
}

void PlanetDemo::creatSkyBox() {
    const auto cube = primitives::cube();
    const auto vertices = map_range(cube.vertices, [](const auto& v){ return v.position.xyz(); });
    const auto indexes = cube.indices;

    skybox.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skybox.indexes = device.createDeviceLocalBuffer(indexes.data(), BYTE_SIZE(indexes), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
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

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;
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
    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    globalDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void PlanetDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { textureDescriptorSetLayout, globalDescriptorSetLayout });
    milkywayDescriptorSet = sets[0];
    globalDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = milkywayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo dispInfo{ milkyway.sampler.handle, milkyway.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &dispInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = globalDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globalBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &globalInfo;

    device.updateDescriptorSets(writes);
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
        render.primitive.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .polygonModeLine()
                .name("primitve_render")
                .build(render.primitive.layout);

        render.skybox.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("skybox/skybox.vert.spv"))
                    .fragmentShader(resource("equi_rect.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
                .rasterizationState()
                    .cullFrontFace()
                .depthStencilState()
                    .compareOpLessOrEqual()
                .layout()
                    .addDescriptorSetLayout(textureDescriptorSetLayout)
                .name("skybox_render")
                .build(render.skybox.layout);
    //    @formatter:on
}


void PlanetDemo::onSwapChainDispose() {
    dispose(render.primitive.pipeline);
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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.primitive.pipeline.handle);
        camera->push(commandBuffer, render.primitive.layout);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_EarthPlanet.m_BaseMesh.vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, m_EarthPlanet.m_BaseMesh.indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);

        renderSkyBox(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PlanetDemo::renderSkyBox(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skybox.layout.handle, 0, 1, &milkywayDescriptorSet, 0, nullptr);
    camera->push(commandBuffer, render.skybox.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skybox.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skybox.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skybox.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
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
        settings.enabledFeatures.shaderFloat64 = true;
        settings.enabledFeatures.shaderInt64 = true;
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