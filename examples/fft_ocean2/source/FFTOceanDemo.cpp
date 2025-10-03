#include "FFTOceanDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

FFTOceanDemo::FFTOceanDemo(const Settings& settings) : VulkanBaseApp("FFT Ocean", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("fft_ocean2");
    fileManager().addSearchPathFront("fft_ocean2/data");
    fileManager().addSearchPathFront("fft_ocean2/spv");
    fileManager().addSearchPathFront("fft_ocean2/models");
    fileManager().addSearchPathFront("fft_ocean2/textures");
}

void FFTOceanDemo::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    initRenderGraphInputs();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    initProfiler();
    initFFTOcean();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void FFTOceanDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.zFar = 10000 * km;
    cameraSettings.zNear = 1;
    cameraSettings.acceleration = glm::vec3(1 * km);
    cameraSettings.velocity = glm::vec3(10 * km);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void FFTOceanDemo::initProfiler() {
    profiler = Profiler{ &device };
    profiler.externalReset = true;
    profiler.paused = true;
}

void FFTOceanDemo::initFFTOcean() {
    ocean = std::make_unique<FFTOcean2>(device, descriptorPool, bindlessDescriptor, *prototypes, *camera, width, height);
    ocean->init();
}

void FFTOceanDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void FFTOceanDemo::beforeDeviceCreation() {
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

void FFTOceanDemo::createDescriptorPool() {
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


void FFTOceanDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void FFTOceanDemo::createDescriptorSetLayouts() {
    displayDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("display_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    subpassInputDescriptorSetLayout =
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
        .createLayout();
}

void FFTOceanDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ displayDescriptorSetLayout, subpassInputDescriptorSetLayout });
    displayDescriptorSet = sets[0];
    subpassInputDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = displayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo displayInfo{ renderGraphInputs.color.sampler.handle, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &displayInfo;

    writes[1].dstSet = subpassInputDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo subpassColorInfo{ VK_NULL_HANDLE, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[1].pImageInfo = &subpassColorInfo;

    writes[2].dstSet = subpassInputDescriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo extrasPosInfo{ VK_NULL_HANDLE, renderGraphInputs.extras.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[2].pImageInfo = &extrasPosInfo;

    device.updateDescriptorSets(writes);
}

void FFTOceanDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void FFTOceanDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void FFTOceanDemo::createRenderPipeline() {
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

        auto builder = prototypes->cloneGraphicsPipeline();
        render.skyView.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("view_direction.vert.spv"))
                    .fragmentShader(resource("sky_view.frag.spv"))
                .depthStencilState()
                    .compareOpLessOrEqual()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .colorBlendState()
                    .attachments(2)
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(AppContext::uniformDescriptorSet())
                    .addDescriptorSetLayout(AppContext::atmosphere().descriptor.uboDescriptorSetLayout)
                    .addDescriptorSetLayout(AppContext::atmosphere().descriptor.lutDescriptorSetLayout)
                .name("sky_view")
                .build(render.skyView.layout);

    render.toneMapper.pipeline =
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
            .addDescriptorSetLayout(subpassInputDescriptorSetLayout)
            .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(render.toneMapper.constants))
        .name("tone_mapper")
    .build(render.toneMapper.layout);
    //    @formatter:on
}


void FFTOceanDemo::onSwapChainDispose() {
    dispose(render.skyView.pipeline);
}

void FFTOceanDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *FFTOceanDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    profiler.resetAll(commandBuffer);

    ocean->preProcess(commandBuffer);

//    runRenderGraph(commandBuffer);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
//        renderToDisplay(commandBuffer);
//        ocean->topView(commandBuffer);
        ocean->render(commandBuffer);
        ocean->preview(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void FFTOceanDemo::runRenderGraph(VkCommandBuffer commandBuffer) {
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
    Offscreen::render(commandBuffer, renderInfo, [&]{
//        renderSkyView(commandBuffer);
//        localReadBarrier(commandBuffer);
        ocean->render(commandBuffer);
        localReadBarrier(commandBuffer);
        toneMap(commandBuffer);
    });
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void FFTOceanDemo::toneMap(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMapper.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.toneMapper.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(render.toneMapper.constants), &render.toneMapper.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMapper.layout.handle, 0, 1, &subpassInputDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void FFTOceanDemo::renderSkyView(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = AppContext::atmosphere().info.descriptorSet;
    sets[1] = AppContext::atmosphere().descriptor.uboDescriptorSet;
    sets[2] = AppContext::atmosphere().descriptor.lutDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skyView.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.skyView.layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
    camera->push(commandBuffer, render.skyView.layout);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void FFTOceanDemo::renderToDisplay(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &displayDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void FFTOceanDemo::renderUI(VkCommandBuffer commandBuffer) {
    static bool oceanOpen = false;
    static bool atmosphereOpen = false;
    static bool lightOpen = false;
    static bool perfOpen = false;
    static bool cloudsOpen = false;

    ImGui::Begin("Controls");
    ImGui::SetWindowSize({0, 0});
    ImGui::Checkbox("Ocean", &oceanOpen);
    ImGui::Checkbox("Atmosphere", &atmosphereOpen);
    ImGui::Checkbox("Lighting", &lightOpen);
    ImGui::Checkbox("clouds", &cloudsOpen);
    ImGui::Checkbox("Performance", &perfOpen);
    ImGui::End();

    ocean->controls(oceanOpen);

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void FFTOceanDemo::update(float time) {
    camera->update(time);
    setTitle(fmt::format("{}, camera - {}, FPS - {}", title, camera->position(), framePerSecond));

}

void FFTOceanDemo::checkAppInputs() {
    camera->processInput();
}

void FFTOceanDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void FFTOceanDemo::onPause() {
    VulkanBaseApp::onPause();
}

void FFTOceanDemo::initRenderGraphInputs() {
    const auto width = swapChain.width();
    const auto height = swapChain.height();
    textures::create(device, renderGraphInputs.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, renderGraphInputs.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});
    textures::create(device, renderGraphInputs.extras, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});

    bindlessDescriptor.update({&renderGraphInputs.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindPoints.colorTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.extras, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindPoints.extraTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, bindPoints.depthTextureIndex});

    renderInfo = Offscreen::RenderInfo{
            .colorAttachments = {
                    {renderGraphInputs.color.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
                    {renderGraphInputs.extras.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
            },
            .depthAttachment = {{renderGraphInputs.depth.imageView, VK_FORMAT_D16_UNORM}},
            .renderArea = {width, height}
    };

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        Barriers::push(renderGraphInputs.extras.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
        Barriers::flush(commandBuffer);
    });

}

void FFTOceanDemo::localReadBarrier(VkCommandBuffer commandBuffer) {
    Barriers::push(
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    Barriers::flush(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT);
}

void FFTOceanDemo::newFrame() {
    camera->newFrame();
    ocean->newFrame();
}

void FFTOceanDemo::endFrame() {
    ocean->endFrame();
    profiler.endFrame();
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
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = FFTOceanDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}