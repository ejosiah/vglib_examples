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
    initObject();
    createDescriptorPool();
    initBindlessDescriptor();
    initRenderGraphInputs();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    auto radius = AppContext::SunAngularRadius * 5;
    AppContext::atmosphere().info.cpu->sunSize = {glm::tan(radius), glm::cos(radius)};
    initLoader();
    initProfiler();
    initFFTOcean();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createComputePipeline();
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
    camera->position({0, 2.33, 88});
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

    objectDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("object_descriptor_set_layout")
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
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    physicsDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("physics_descriptor_set_layout")
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
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void FFTOceanDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({
        displayDescriptorSetLayout, subpassInputDescriptorSetLayout, objectDescriptorSetLayout,
        physicsDescriptorSetLayout
    });
    displayDescriptorSet = sets[0];
    subpassInputDescriptorSet = sets[1];
    objectDescriptorSet = sets[2];
    physicsDescriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<13>();

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
    
    writes[3].dstSet = objectDescriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo vertexInfo{ object.vertices, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &vertexInfo;

    writes[4].dstSet = objectDescriptorSet;
    writes[4].dstBinding = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo indexInfo{ object.indexes, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &indexInfo;

    writes[5].dstSet = objectDescriptorSet;
    writes[5].dstBinding = 2;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo pointInfo{ object.points, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &pointInfo;

    writes[6].dstSet = objectDescriptorSet;
    writes[6].dstBinding = 3;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo areaInfo{ object.area, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &areaInfo;

    writes[7].dstSet = objectDescriptorSet;
    writes[7].dstBinding = 4;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo info{ object.metadata, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &info;

    writes[8].dstSet = physicsDescriptorSet;
    writes[8].dstBinding = 0;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    VkDescriptorBufferInfo samplePointsInfo{ physics.samplePoints, 0, VK_WHOLE_SIZE };
    writes[8].pBufferInfo = &samplePointsInfo;

    writes[9].dstSet = physicsDescriptorSet;
    writes[9].dstBinding = 1;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo sampleAreasInfo{ physics.sampleArea, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &sampleAreasInfo;

    writes[10].dstSet = physicsDescriptorSet;
    writes[10].dstBinding = 2;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ physics.counts, 0, VK_WHOLE_SIZE };
    writes[10].pBufferInfo = &countsInfo;

    writes[11].dstSet = physicsDescriptorSet;
    writes[11].dstBinding = 3;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo stagingInfo{ physics.staging, 0, VK_WHOLE_SIZE };
    writes[11].pBufferInfo = &stagingInfo;

    writes[12].dstSet = physicsDescriptorSet;
    writes[12].dstBinding = 4;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo impulsePointsInfo{ physics.impulsePointsBuffer, 0, VK_WHOLE_SIZE };
    writes[12].pBufferInfo = &impulsePointsInfo;

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

    render.arealPerspective.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
        .shaderStage()
            .vertexShader(resource("quad.vert.spv"))
            .fragmentShader(resource("areal_perspective.frag.spv"))
        .dynamicRenderPass()
            .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
            .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
            .depthAttachment(VK_FORMAT_D16_UNORM)
        .depthStencilState()
            .disableDepthTest()
            .disableDepthWrite()
        .colorBlendState()
            .attachment().clear()
                .enableBlend()
                .colorBlendOp().add()
                .alphaBlendOp().add()
                .srcColorBlendFactor().one()
                .dstColorBlendFactor().srcAlpha()
                .srcAlphaBlendFactor().one()
                .dstAlphaBlendFactor().one()
            .add()
            .attachment().add()
        .layout()
            .addDescriptorSetLayout(AppContext::uniformDescriptorSet())
            .addDescriptorSetLayout(AppContext::atmosphere().descriptor.uboDescriptorSetLayout)
            .addDescriptorSetLayout(AppContext::atmosphere().descriptor.lutDescriptorSetLayout)
            .addDescriptorSetLayout(subpassInputDescriptorSetLayout)
        .name("areal_perspective")
    .build(render.arealPerspective.layout);

    render.object.pipeline =
        prototypes->cloneGraphicsPipeline()
        .shaderStage()
            .vertexShader(resource("render_object.vert.spv"))
            .fragmentShader(resource("render_object.frag.spv"))
        .dynamicRenderPass()
            .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
            .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
            .depthAttachment(VK_FORMAT_D16_UNORM)
        .layout()
            .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::vec4))
        .name("render_object")
    .build(render.object.layout);

    //    @formatter:on
}


void FFTOceanDemo::onSwapChainDispose() {
    dispose(render.skyView.pipeline);
}

void FFTOceanDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    updateDescriptorSets();
    createRenderPipeline();
    ocean->refresh(*prototypes);
}

VkCommandBuffer *FFTOceanDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    profiler.resetAll(commandBuffer);

    ocean->preProcess(commandBuffer);
//    computeBuoyancy(commandBuffer);

    runRenderGraph(commandBuffer);

    clearColor(0, 0, 1);
    renderToSwapChain([&]{
        renderToDisplay(commandBuffer);
        ocean->renderTopView(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void FFTOceanDemo::renderObjects(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    auto lightDir = AppContext::atmosphere().info.cpu->sunDirection;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.object.pipeline.handle);
    camera->push(commandBuffer, render.object.layout, object.info->transform);
    vkCmdPushConstants(commandBuffer, render.object.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::vec4), &lightDir);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, object.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, object.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, object.indexes.sizeAs<uint>(), 1, 0, 0, 0);
}

void FFTOceanDemo::runRenderGraph(VkCommandBuffer commandBuffer) {
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
    Offscreen::render(commandBuffer, renderInfo, [&]{
//        renderObjects(commandBuffer);
        ocean->render(commandBuffer);

        renderSkyView(commandBuffer);
        localReadBarrier(commandBuffer);

        renderArealPerspective(commandBuffer);
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

void FFTOceanDemo::renderArealPerspective(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = AppContext::atmosphere().info.descriptorSet;
    sets[1] = AppContext::atmosphere().descriptor.uboDescriptorSet;
    sets[2] = AppContext::atmosphere().descriptor.lutDescriptorSet;
    sets[3] = subpassInputDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.arealPerspective.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.arealPerspective.layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void FFTOceanDemo::renderToDisplay(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &displayDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void FFTOceanDemo::renderUI(VkCommandBuffer commandBuffer) {
    static bool oceanOpen = true;

    ocean->controls(oceanOpen);
    ocean->visualizer(plugin<ImGuiPlugin>(IM_GUI_PLUGIN));
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void FFTOceanDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
//    auto impulseCount = physics.sizes[IMPULSE_COUNT];
    setTitle(fmt::format("{}, camera - {}, FPS - {}", title, camera->position(), framePerSecond));

//    static constexpr auto dt = 0.00833333f;
//    static const glm::vec3 Gravity{0, -9.8, 0};
//    static const glm::vec3 GravityImpulse = (1.0f/object.body.m_invMass) * Gravity * dt;
//    static constexpr float waterDensity = 1000;
//    object.body.ApplyImpulseLinear(GravityImpulse);
//
//    impulseCount = std::min(impulseCount, 10u);
//    for(auto i = 0; i < impulseCount; ++i) {
//        auto impulse = object.body.m_elasticity * physics.impulses[i];
//        auto impulsePoint =  physics.impulsePoints[i];
//        spdlog::info("impulse: {}", impulse);
//        object.body.ApplyImpulse(impulsePoint, impulse);
//        if(i >= impulseCount - 1) spdlog::info("");
//    }
//
//    object.body.Update(dt);
//    object.info->transform = glm::translate(glm::mat4{1}, object.body.m_position) * glm::mat4(object.body.m_orientation);

}

void FFTOceanDemo::checkAppInputs() {
    camera->processInput();

    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        ocean->updateMouse(glm::ivec2{mouse.position}, 1);
    }else {
        ocean->updateMouse(glm::ivec2{0});
    }
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
    auto& info = *AppContext::atmosphere().info.cpu;
    info.inverse_model = glm::inverse(camera->cam().model);
    info.inverse_view = glm::inverse(camera->cam().view);
    info.inverse_projection = glm::inverse(camera->cam().proj);
    info.camera = glm::vec4{ camera->position(), 1 };
    camera->newFrame();
    ocean->newFrame();
}

void FFTOceanDemo::endFrame() {
    ocean->endFrame();
    profiler.endFrame();
}

void FFTOceanDemo::initObject() {
    auto r = 20.0f;
    glm::vec3 dim{305, 95, 60};
    auto primitive = primitives::cube({1, 1, 0, 1}, glm::scale(glm::mat4{1}, dim));
//    auto sphere = primitives::sphere(500, 500, r, glm::mat4{1}, {1, 0, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    auto numTris = to<uint>(primitive.indices.size()/3);

    Info info{};
    object.vertices = device.createDeviceLocalBuffer(primitive.vertices.data(), BYTE_SIZE(primitive.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    object.indexes = device.createDeviceLocalBuffer(primitive.indices.data(), BYTE_SIZE(primitive.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    object.area = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(float) * numTris, "object_area");
    object.points = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec3) * numTris, "object_points");
    object.metadata = device.createCpuVisibleBuffer(&info, sizeof(Info), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    object.info = reinterpret_cast<Info*>(object.metadata.map());

    auto mass = 205930936.0f;
    auto points = map_range(primitive.vertices, [](auto v){ return v.position.xyz(); });
    object.body.m_shape = std::make_shared<ShapeBox>(points.data(), to<int>(points.size()));
    object.body.m_invMass  = 1.0f/mass;
    object.body.m_elasticity = 0.6;
    object.body.m_position = glm::vec3{0, 0, 0};
//    object.info->density = (3.0f * mass)/(4.0f * glm::pi<float>() * r * r * r);
    object.info->density = (3.0f * mass)/(dim.x * dim.x * dim.z);
    object.info->numTris = numTris;

    int numCounters = 2;
    physics.samplePoints = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec3) * numTris, "sample_points");
    physics.sampleArea = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(float) * numTris, "sample_areas");
    physics.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(uint) * numCounters, "sample_counts");
    physics.staging = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(vec3) * numTris, "staging_buffer");
    physics.impulsePointsBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(vec3) * numTris, "impulse_points");

    physics.impulses = physics.staging.span<glm::vec3>();
    physics.impulsePoints = physics.impulsePointsBuffer.span<glm::vec3>();
    physics.sizes = physics.counts.span<uint>();
    spdlog::error("object density: {}, triangle count: {}", object.info->density, numTris);

}

void FFTOceanDemo::createComputePipeline() {
    compute = ComputePipelines{&device, metadata()};
    compute.createPipelines();
}

std::vector<PipelineMetaData> FFTOceanDemo::metadata() {
    buoyancyConstants.horizontalLength = ocean->patchLengths();
    buoyancyConstants.heightMapIndex = ocean->heightMapTextureIndex();
    return {
        {
            .name = "update_object",
            .shadePath = resource("update_object.comp.spv"),
            .layouts = { &objectDescriptorSetLayout, &physicsDescriptorSetLayout },
        },
        {
            .name = "sample_points",
            .shadePath = resource("sample_points.comp.spv"),
            .layouts = { &objectDescriptorSetLayout, &physicsDescriptorSetLayout },
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float)} }
        },
        {
            .name = "surface_area",
            .shadePath = resource("compute_surface_area.comp.spv"),
            .layouts = { &objectDescriptorSetLayout, &physicsDescriptorSetLayout },
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint)} }
        },
        {
            .name = "buoyancy",
            .shadePath = resource("compute_buoyancy.comp.spv"),
            .layouts = { &objectDescriptorSetLayout, &physicsDescriptorSetLayout, const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout) },
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint)} }
        },
    };
}

void FFTOceanDemo::computeBuoyancy(VkCommandBuffer commandBuffer) {
    updateObjects(commandBuffer);
    samplePoints(commandBuffer);
    computeSurfaceArea(commandBuffer);
    generateImpulses(commandBuffer);
}


void FFTOceanDemo::updateObjects(VkCommandBuffer commandBuffer) {
    const auto height = 4 * m;
    const auto gx = to<uint>(std::ceil(object.info->numTris/1024.0));

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = objectDescriptorSet;
    sets[1] = physicsDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("update_object") );
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("update_object"), 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}



void FFTOceanDemo::samplePoints(VkCommandBuffer commandBuffer) {
    const auto height = 4 * m;
    const auto gx = to<uint>(std::ceil(object.info->numTris/1024.0));

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = objectDescriptorSet;
    sets[1] = physicsDescriptorSet;

    vkCmdFillBuffer(commandBuffer, physics.counts, 0, physics.counts.size, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("sample_points") );
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("sample_points"), 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute.layout("sample_points"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &height);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void FFTOceanDemo::computeSurfaceArea(VkCommandBuffer commandBuffer) {
    const auto gx = to<uint>(std::ceil(object.info->numTris/1024.0));

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = objectDescriptorSet;
    sets[1] = physicsDescriptorSet;

    auto pass = 0u;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("surface_area") );
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("surface_area"), 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute.layout("surface_area"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint), &pass);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);

    if(gx > 1) {
        pass = 1u;
        vkCmdPushConstants(commandBuffer, compute.layout("surface_area"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint), &pass);
        vkCmdDispatch(commandBuffer, 1, 1, 1);
        Barrier::computeWriteToRead(commandBuffer);
    }
}

void FFTOceanDemo::generateImpulses(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = objectDescriptorSet;
    sets[1] = physicsDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;

    const auto gx = to<uint>(std::ceil(object.info->numTris/1024.0));
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("buoyancy") );
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("buoyancy"), 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute.layout("buoyancy"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(buoyancyConstants), &buoyancyConstants);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
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