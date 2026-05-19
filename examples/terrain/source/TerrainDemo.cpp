#include "TerrainDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

namespace {
    constexpr glm::ivec2 TerrainWorldSize{52660};
    constexpr glm::vec2 TerrainHeightScale{-14.0f, 1587.0f};
}

TerrainDemo::TerrainDemo(const Settings& settings) : VulkanBaseApp("Terrain", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/textures/height_map");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("common/spv");
    fileManager().addSearchPathFront("terrain");
    fileManager().addSearchPathFront("terrain/data");
    fileManager().addSearchPathFront("terrain/spv");
    fileManager().addSearchPathFront("terrain/models");
    fileManager().addSearchPathFront("terrain/textures");
}

void TerrainDemo::initApp() {
    initProfiler();
    createSamplers();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    createComputePipelines();
    initContext();
    initGBuffer();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initDisplacementMapGenerator();
    initAtmosphere();
    initTerrain();
    initDisplacementShadowMap();
    initClouds();
    clearColor(0, 0, 1);
}

void TerrainDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.zFar = 10000 * km;
    cameraSettings.zNear = 1;
    cameraSettings.acceleration = glm::vec3(1 * km);
    cameraSettings.velocity = glm::vec3(10 * km);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->lookAt({3732, 33.5, 16265}, {-0.69, 0.02, -0.7}, {0, 1, 0});
     camera->lookAt({-1014, 127.6, 12620}, {0.299, -0.16, -0.939}, {0, 1, 0});
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

    auto localReads = findExtension<VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR, deviceCreateNextChain);
    localReads->dynamicRenderingLocalRead = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void TerrainDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 200;
    std::array<VkDescriptorPoolSize, 5> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}


void TerrainDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void TerrainDemo::createDescriptorSetLayouts() {
    displayDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("display_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    context.subpassInputDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("input_attachment_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

}

void TerrainDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ displayDescriptorSetLayout, context.subpassInputDescriptorSetLayout });
    displayDescriptorSet = sets[0];
    context.subpassInputDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = displayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo displayInfo{ renderGraphInputs.color.sampler.handle, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &displayInfo;

    writes[1].dstSet = context.subpassInputDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo subpassColorInfo{ VK_NULL_HANDLE, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[1].pImageInfo = &subpassColorInfo;

    writes[2].dstSet = context.subpassInputDescriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo subpassPosInfo{ VK_NULL_HANDLE, renderGraphInputs.position.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[2].pImageInfo = &subpassPosInfo;

    writes[3].dstSet = context.subpassInputDescriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo subpassDepthInfo{ VK_NULL_HANDLE, renderGraphInputs.depth.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[3].pImageInfo = &subpassDepthInfo;

    device.updateDescriptorSets(writes);
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
    render.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("quad.frag.spv"))
            .layout()
                .addDescriptorSetLayout(displayDescriptorSetLayout)
            .name("lighting")
        .build(render.layout);

    toneMapper.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("tone_mapping.frag.spv"))
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .depthStencilState()
                .compareOpAlways()
            .colorBlendState()
                .attachments(2)
            .layout()
                .addDescriptorSetLayout(context.subpassInputDescriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapper.constants))
            .name("tone_mapper")
        .build(toneMapper.layout);
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

    profiler.resetAll(commandBuffer);

    displacementShadowMap->setDisplacementScale(terrain->displacementScale());
    displacementShadowMap->exec(commandBuffer);
    atmosphere->preProcess(commandBuffer);
    terrain->preProcess(commandBuffer);

    runRenderGraph(commandBuffer);

    renderToSwapChain([&]{
        renderToDisplay(commandBuffer);
        terrain->renderTopView(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}


void TerrainDemo::runRenderGraph(VkCommandBuffer commandBuffer) {
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
    Offscreen::render(commandBuffer, renderInfo, [&]{
        terrain->render(commandBuffer);
        atmosphere->renderSkyView(commandBuffer);
        localReadBarrier(commandBuffer);
        clouds->render(commandBuffer);
        localReadBarrier(commandBuffer);
        atmosphere->renderArealPerspective(commandBuffer);
        localReadBarrier(commandBuffer);
        toneMap(commandBuffer);
    });
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void TerrainDemo::renderToDisplay(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &displayDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void TerrainDemo::renderUI(VkCommandBuffer commandBuffer) {

    static bool terrainOpen = false;
    static bool atmosphereOpen = false;
    static bool lightOpen = false;
    static bool perfOpen = false;
    static bool cloudsOpen = false;

    ImGui::Begin("Controls");
    ImGui::SetWindowSize({0, 0});
    ImGui::Checkbox("Terrain", &terrainOpen);
    ImGui::Checkbox("Atmosphere", &atmosphereOpen);
    ImGui::Checkbox("Lighting", &lightOpen);
    ImGui::Checkbox("clouds", &cloudsOpen);
    ImGui::Checkbox("Performance", &perfOpen);
    ImGui::End();

    terrain->controls(terrainOpen);
    atmosphere->controls(atmosphereOpen);
    clouds->controls(cloudsOpen);

    if(lightOpen) {
        ImGui::Begin("Lighting");
        ImGui::SetWindowSize({0, 0});
        ImGui::SliderFloat("Zenith Angle", &options.lightZenith, -90, 180);
        ImGui::SliderFloat("Azimuth Angle", &options.lightAzimuth, 0, 360);
        terrain->lightingControls();
        displacementShadowMap->controls();

        if (ImGui::CollapsingHeader("ToneMapping", ImGuiTreeNodeFlags_DefaultOpen)) {
            static std::array<const char *, 5> labels{"Clamp", "Reinhard", "Uncharted 2", "ACES",
                                                      "Hejl-Burgess-Dawson"};
            ImGui::Combo("Tone mapper", &toneMapper.constants.method, labels.data(), labels.size());
            ImGui::SliderFloat("Exposure Value", &toneMapper.constants.exposureValue, -3, 3);
        }
        ImGui::Checkbox("Debug", &options.debug);
        ImGui::End();   // End lighting
    }

    if(perfOpen) {
        ImGui::Begin("performance");
        ImGui::SetWindowSize({0, 0});
        auto total = 0.0f;
        total += displacementShadowMap->printPerfStats();
        total += terrain->printPerfStats();
        total += atmosphere->printPerfStats();
        total += clouds->printPerfStats();

        ImGui::Text("total frame time: %f ms", total);
        ImGui::End();
    }

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void TerrainDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()){
        camera->update(time);
    }
    context.elapsedTime = time;
    setTitle(fmt::format("{}, camera - {}, direction - {}, lightDirection - {}, nodes - {}, FPS - {}", title, camera->position(), camera->viewDir, lightDirection, terrain->nodeCount(), framePerSecond));

//    static auto g = glm::vec3{0, -9.8 * m, 0};
//    static auto v = glm::vec3{0};;
//    v += g * time;
//    camera->position(camera->position() + v * time);
}

void TerrainDemo::checkAppInputs() {
    camera->processInput();
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        context.mouse = glm::ivec4{mouse.position, 1, 0};
    }else {
        context.mouse = glm::ivec4{0};
    }
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
    context.mouseInput = &mouse;
    context.device = &device;
    context.descriptorPool = &descriptorPool;
    context.camera = camera.get();
    context.rgInputs = &renderGraphInputs;
    context.edgeClampSampler = edgeClampSampler;
    context.bindlessDescriptor = &bindlessDescriptor;
    context.prototypes = std::make_unique<Prototypes>(device, swapChain, renderPass);
    context.lightDirection = glm::normalize(glm::vec3{1});
    context.dmap_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_normal_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_slope_moments0_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_slope_moments1_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_shadow_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.transmittanceTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.multiScatteringTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.skyViewTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.arealPerspectiveTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.radianceTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.positionTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.depthTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.profiler = &profiler;
}

void TerrainDemo::initTerrain() {
    terrain = std::make_unique<Terrain>(context, atmosphere->descriptor(), TerrainWorldSize, TerrainHeightScale);
    terrain->init();
}

void TerrainDemo::initDisplacementMapGenerator() {
    auto path = "kauai.png";
    displacementMapGenerator = std::make_unique<DisplacementMapGenerator>(context, DisplacementMethod::File, 3601, 3601, resource(path));
    displacementMapGenerator->setTerrainMetrics(glm::vec2{static_cast<float>(TerrainWorldSize.x), static_cast<float>(TerrainWorldSize.y)}, TerrainHeightScale);
    displacementMapGenerator->init();
}

void TerrainDemo::initAtmosphere() {
    atmosphere = std::make_unique<AtmosphereModel>(context);
    atmosphere->init();
}

void TerrainDemo::endFrame() {
    terrain->endFrame();
    clouds->endFrame();
    profiler.endFrame();
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
    context.inverseView = glm::inverse(cam.view);
    context.inverseProjection = glm::inverse(cam.proj);
    Frustum::extractFrustum(context.viewProjectionFrustum, context.viewProjection);

    atmosphere->newFrame();
    terrain->newFrame();
    clouds->newFrame();
}

void TerrainDemo::createComputePipelines() {
//    compute = ComputePipelines(&device, {{
//         .name = "generate_normals",
//         .shadePath = resource("vista_generate_normal_map.comp.spv"),
//         .layouts = { const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout)},
//         .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int) * 3} }
//     }});
//    compute.createPipelines();
}

void TerrainDemo::initDisplacementShadowMap() {
    auto dmapInfo = displacementMapGenerator->displacementMapInfo();
    auto terrainInfo = terrain->getInfo();

    displacementShadowMap = std::make_unique<DisplacementShadowMap>(context, dmapInfo, terrainInfo);
    displacementShadowMap->init();
}

void TerrainDemo::initGBuffer() {
    const auto width = swapChain.width();
    const auto height = swapChain.height();

    textures::create(device, renderGraphInputs.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, renderGraphInputs.position, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, renderGraphInputs.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});
    textures::create(device, renderGraphInputs.depth1, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    bindlessDescriptor.update({&renderGraphInputs.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.radianceTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.position, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.positionTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.depthTextureIndex});

    renderInfo = Offscreen::RenderInfo{
            .colorAttachments = {
                    {renderGraphInputs.color.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
                    {renderGraphInputs.position.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
            },
            .depthAttachment = {{renderGraphInputs.depth.imageView, VK_FORMAT_D16_UNORM}},
            .renderArea = {width, height}
    };

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        auto subresource = DEFAULT_SUB_RANGE;
        subresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        Barriers::push(renderGraphInputs.depth.image, subresource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
        Barriers::push(renderGraphInputs.position.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
        Barriers::flush(commandBuffer);
    });
}


void TerrainDemo::localReadBarrier(VkCommandBuffer commandBuffer) {
    Barriers::push(
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    Barriers::flush(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT);

}

void TerrainDemo::toneMap(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, toneMapper.pipeline.handle);
    vkCmdPushConstants(commandBuffer, toneMapper.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapper.constants), &toneMapper.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, toneMapper.layout.handle, 0, 1, &context.subpassInputDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void TerrainDemo::createSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1;

    edgeClampSampler = device.createSampler(samplerInfo);
}

void TerrainDemo::initProfiler() {
    profiler = Profiler{ &device };
    profiler.externalReset = true;
}

void TerrainDemo::initClouds() {
    clouds = std::make_unique<Clouds>(context, atmosphere->descriptor());
    clouds->init();
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
        settings.enabledFeatures.independentBlend = true;
        settings.enabledFeatures.pipelineStatisticsQuery = true;
        settings.enabledFeatures.occlusionQueryPrecise = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME);
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
