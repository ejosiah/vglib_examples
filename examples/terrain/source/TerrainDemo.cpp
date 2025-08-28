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
    fileManager().addSearchPathFront("../data/textures/height_map");
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
    createComputePipelines();
    initContext();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initDisplacementMapGenerator();
    initTerrain();
    initDisplacementShadowMap();
}

void TerrainDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.zFar = 64000;
    cameraSettings.acceleration = glm::vec3(500);
    cameraSettings.velocity = glm::vec3(1000);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->position({-1130, 3796, 5057});
    camera->lookAt({-901, 215, 4442}, glm::vec3(0.2, 0.1, -1.0), {0, 1, 0});
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

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

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

    displacementShadowMap->exec(commandBuffer);
    terrain->preProcess(commandBuffer);

    clearColor(0, 0, 1);
    renderToSwapChain([&]{
        terrain->render(commandBuffer);
        AppContext::renderAtmosphere(commandBuffer, *camera);
        terrain->renderTopView(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void TerrainDemo::renderUI(VkCommandBuffer commandBuffer) {
    terrain->controls();
    displacementShadowMap->controls();
    ImGui::Begin("Lighting");
    ImGui::SetWindowSize({0, 0});
    ImGui::SliderFloat("Zenith Angle", &options.lightZenith, 0, 180);
    ImGui::SliderFloat("Azimuth Angle", &options.lightAzimuth, 0, 360);
    ImGui::End();   // End lighting

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void TerrainDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()){
        camera->update(time);
    }

    setTitle(fmt::format("{}, camera - {}, direction - {}, lightDirection - {}, nodes - {}, FPS - {}", title, camera->position(), camera->viewDir, lightDirection, terrain->nodeCount(), framePerSecond));
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
    context.lightDirection = glm::normalize(glm::vec3{1});
    context.dmap_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_normal_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_shadow_tex_index = bindlessDescriptor.reserveTextureSlots(1);
}

void TerrainDemo::initTerrain() {
    terrain = std::make_unique<Terrain>(context);
    terrain->init();
}

void TerrainDemo::initDisplacementMapGenerator() {
    auto path = "kauai.png";
    displacementMapGenerator = std::make_unique<DisplacementMapGenerator>(context, DisplacementMethod::File, 3601, 3601, resource(path));
    displacementMapGenerator->init();
}

void TerrainDemo::endFrame() {

    terrain->endFrame();
}

void TerrainDemo::newFrame() {
    camera->newFrame();
    auto& cam = camera->cam();

    glm::mat4 rot = glm::rotate(glm::mat4{1}, glm::radians(options.lightAzimuth), {0, 1, 0});
    rot = glm::rotate(rot, glm::radians(options.lightZenith), {0, 0, 1});
    lightDirection = (rot * glm::vec4{1, 0, 0, 1}).xyz();

    context.lightDirection = lightDirection;
    context.view = cam.view;
    context.viewProjection = cam.proj * cam.view;
    context.inverseViewProjection = glm::inverse(context.viewProjection);
    Frustum::extractFrustum(context.viewProjectionFrustum, context.viewProjection);

    AppContext::updateSunDirection(lightDirection);
    terrain->newFrame();
}

void TerrainDemo::createComputePipelines() {
    compute = ComputePipelines(&device, {{
         .name = "generate_normals",
         .shadePath = resource("generate_normal_map.comp.spv"),
         .layouts = { const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout)},
         .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int) * 3} }
     }});
    compute.createPipelines();
}

void TerrainDemo::initDisplacementShadowMap() {
    auto dmapInfo = displacementMapGenerator->displacementMapInfo();
    auto terrainInfo = terrain->getInfo();

    displacementShadowMap = std::make_unique<DisplacementShadowMap>(context, dmapInfo, terrainInfo);
    displacementShadowMap->init();
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