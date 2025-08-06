#include "TemporalAntiAliasingExample.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"
#include "AppContext.hpp"

TemporalAntiAliasingExample::TemporalAntiAliasingExample(const Settings& settings) : VulkanBaseApp("Temporal Anti-Aliasing", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/textures/environment");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../common/spv");
    fileManager().addSearchPathFront("temporal_anti_aliasing");
    fileManager().addSearchPathFront("temporal_anti_aliasing/data");
    fileManager().addSearchPathFront("temporal_anti_aliasing/spv");
    fileManager().addSearchPathFront("temporal_anti_aliasing/models");
    fileManager().addSearchPathFront("temporal_anti_aliasing/textures");
}

void TemporalAntiAliasingExample::initApp() {
    initBindlessDescriptor();
    createDescriptorPool();
    AppContext::init(device, _descriptorPool, swapChain, renderPass);
    initJitter();
    initLoader();
    initTextures();
    initCanvas();
    initScreenQuad();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    loadModel();
    initCamera();
    createPipelineCache();
    createRenderPipeline();
    initTaa();
}

void TemporalAntiAliasingExample::initTaa() {
    taa_settings.resolution = { width, height};
    taa = std::make_unique<taa::Taa>(device, _descriptorPool, _bindlessDescriptor, _textures.color, _textures.depth, *_camera, jitterValue, taa_settings);
    taa->init();
}

void TemporalAntiAliasingExample::initLoader() {
    _loader = std::make_unique<gltf::Loader>(&device, &_descriptorPool, &_bindlessDescriptor);
    _loader->start();
}

void TemporalAntiAliasingExample::initJitter() {

}

void TemporalAntiAliasingExample::loadModel() {
//    _model = _loader->loadGltf(resource("Sponza/glTF/Sponza.gltf"));
    _model = _loader->loadGltf(resource("FlightHelmet/glTF/FlightHelmet.gltf"));
//    _model = _loader->loadGltf(resource("ABeautifulGame/glTF/ABeautifulGame.gltf"));
    _model->transform = glm::translate(glm::mat4{1}, -_model->bounds.min);
    _model->sync();
}

void TemporalAntiAliasingExample::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
//    cameraSettings.velocity = glm::vec3(20);
//    cameraSettings.acceleration = glm::vec3(10);

    _camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);

    auto target = (_model->bounds.min + _model->bounds.max) * 0.5f;
    auto position = target - glm::vec3(0, 0, -1);

    _camera->lookAt(position, target, {0, 1, 0});
}

void TemporalAntiAliasingExample::initScreenQuad() {
    auto vertices = ClipSpace::Quad::positions;
    _offScreenQuad  = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void TemporalAntiAliasingExample::initTextures() {
    _textures.width = swapChain.width();
    _textures.height = swapChain.height();
    auto format = VK_FORMAT_D16_UNORM;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = 0;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = { _textures.width, _textures.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    _textures.depth.image = device.createImage(imageInfo);


    VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    _textures.depth.imageView = _textures.depth.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);
    device.setName<VK_OBJECT_TYPE_IMAGE>("depth_buffer", _textures.depth.image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("depth_buffer_view", _textures.depth.imageView.handle);

    format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.format = format;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    _textures.color.image = device.createImage(imageInfo);
    _textures.color.format = format;


    resourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    _textures.color.imageView = _textures.color.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    device.setName<VK_OBJECT_TYPE_IMAGE>("color_buffer", _textures.color.image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("color_buffer_view", _textures.color.imageView.handle);

    _textures.color.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    _textures.color.sampler = device.createSampler(samplerInfo);

    _offscreenInfo = Offscreen::RenderInfo{
        .colorAttachments = {{ _textures.color.imageView, VK_FORMAT_R32G32B32A32_SFLOAT}},
        .depthAttachment = { { _textures.depth.imageView, VK_FORMAT_D16_UNORM} },
        .renderArea = { _textures.width, _textures.height }
    };

}

void TemporalAntiAliasingExample::initCanvas() {
    _canvas = Canvas{ this };
    _canvas.init();
    
    auto sets = _descriptorPool.allocate( { _canvas.getDescriptorSetLayout() } );
    _displaySet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = _displaySet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo info{ _textures.color.sampler.handle, _textures.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &info;

    device.updateDescriptorSets(writes);
}


void TemporalAntiAliasingExample::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 4> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
            }
    };
    _descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void TemporalAntiAliasingExample::createDescriptorSetLayouts() {


}

void TemporalAntiAliasingExample::updateDescriptorSets(){

}

void TemporalAntiAliasingExample::createCommandPool() {
    _commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    _commandBuffers = _commandPool.allocateCommandBuffers(swapChainImageCount);
}

void TemporalAntiAliasingExample::createPipelineCache() {
    _pipelineCache = device.createPipelineCache();
}


void TemporalAntiAliasingExample::createRenderPipeline() {
    //    @formatter:off

        auto width = _textures.width;
        auto height = _textures.height;
        auto builder = device.graphicsPipelineBuilder();
        _render.ground.pipeline =
            builder
                .allowDerivatives()
                .shaderStage()
                    .vertexShader(resource("ground.vert.spv"))
                    .fragmentShader(resource("ground.frag.spv"))
                .vertexInputState()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .viewportState()
                    .viewport()
                        .origin(0, 0)
                        .dimension(width, height)
                        .minDepth(0)
                        .maxDepth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(width, height)
                    .add()
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLessOrEqual()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_ALL_GRAPHICS))
                .subpass(0)
                .name("ground_pipeline")
            .build(_render.ground.layout);

        _render.model.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
                .inputAssemblyState()
                    .triangles()
                .rasterizationState()
                    .cullBackFace()
                .colorBlendState()
                    .attachment().clear()
                    .enableBlend()
                    .colorBlendOp().add()
                    .alphaBlendOp().add()
                    .srcColorBlendFactor().srcAlpha()
                    .dstColorBlendFactor().oneMinusSrcAlpha()
                    .srcAlphaBlendFactor().zero()
                    .dstAlphaBlendFactor().one()
                .add()
                .dynamicState()
                    .colorBlendEnable()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(_loader->descriptorSetLayout())
                    .addDescriptorSetLayout(_loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*_bindlessDescriptor.descriptorSetLayout)
                .name("model_pipeline")
            .build(_render.model.layout);

        _render.placeHolder.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
                .inputAssemblyState()
                    .lines()
                .rasterizationState()
                    .cullNone()
                    .lineWidth(5)
                    .colorBlendState()
                    .attachment().clear()
                .add()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                .name("model_placeholder")
            .build(_render.placeHolder.layout);
    //    @formatter:on
}

void TemporalAntiAliasingExample::onSwapChainDispose() {
    dispose(_render.model.pipeline);
    dispose(_render.ground.pipeline);
}

void TemporalAntiAliasingExample::onSwapChainRecreation() {
    initTextures();
    initCanvas();
    updateDescriptorSets();
    createRenderPipeline();
    _camera->perspective(swapChain.aspectRatio());
    taa->resize({ width, height});
}

VkCommandBuffer *TemporalAntiAliasingExample::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = _commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.2, 0.2, 0.2, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    _canvas.draw(commandBuffer, _displaySet);

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    offscreenRender(commandBuffer);
    applyTaa(commandBuffer);

    vkEndCommandBuffer(commandBuffer);


    return &commandBuffer;
}

void TemporalAntiAliasingExample::offscreenRender(VkCommandBuffer commandBuffer) {
    static Offscreen offscreen;

    offscreen.render(commandBuffer, _offscreenInfo, [&]{
        renderGround(commandBuffer);
//        renderPlaceHolders(commandBuffer);
        renderScene(commandBuffer);
    });
}

void TemporalAntiAliasingExample::renderGround(VkCommandBuffer commandBuffer) {
    static glm::mat4 identity{1};
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.ground.pipeline.handle);
    _camera->push(commandBuffer, _render.ground.layout, identity, VK_SHADER_STAGE_ALL_GRAPHICS);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _offScreenQuad, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void TemporalAntiAliasingExample::renderScene(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;

    sets[0] = _model->meshDescriptorSet.u16.handle;
    sets[1] = _model->materialDescriptorSet;
    sets[2] = _bindlessDescriptor.descriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.pipeline.handle);
    _camera->push(commandBuffer, _render.model.layout);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _model->vertices, &offset);

    VkBool32 blendingEnabled = VK_TRUE;
    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);
    vkCmdBindIndexBuffer(commandBuffer, _model->indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw.u16.handle, 0, _model->draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = _model->meshDescriptorSet.u32.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, _model->indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw.u32.handle, 0, _model->draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = _model->meshDescriptorSet.u8.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, _model->indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw.u8.handle, 0, _model->draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));

    _model->renderWithMaterial(commandBuffer, _render.model.layout, 0, true);

}

void TemporalAntiAliasingExample::renderUI(VkCommandBuffer commandBuffer) {

    ImGui::Begin("TAA");
    ImGui::SetWindowSize({0, 0});
    ImGui::Checkbox("Enable", &options.taaEnabled);
    ImGui::Checkbox("Jittering", &options.jitterEnabled);
    ImGui::Combo("Sampler", &options.samplerType, options.samplers.data(), options.samplers.size());
    ImGui::SliderInt("period", &options.jitterPeriod, 2, 16);

    if(!options.simple) {
        options.dirty |= ImGui::Combo("Filter", &options.filter, options.filters.data(), options.filters.size());
        options.dirty |= ImGui::Combo("Sub-Sample Filter", &options.sub_sample_filter, options.subSampleFilters.data(), options.subSampleFilters.size());
        options.dirty |= ImGui::Combo("History Constraint", &options.history_constraint, options.historyConstraints.data(), options.historyConstraints.size());

        ImGui::Text("Blending Weights filtering:");
        ImGui::Indent(16);
        options.dirty |= ImGui::Checkbox("Temporal", &options.temporal_filtering);
        ImGui::SameLine();
        options.dirty |= ImGui::Checkbox("Inverse Luminance", &options.inverse_luminance_filtering);
        ImGui::SameLine();
        options.dirty |= ImGui::Checkbox("Luminance Difference", &options.luminance_difference_filtering);
        ImGui::Indent(-16);
    }

    options.dirty |= ImGui::Checkbox("Simple TAA", &options.simple);

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void TemporalAntiAliasingExample::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        _camera->update(time);
    }

    glfwSetWindowTitle(window, fmt::format("{}, fps: {}", title, framePerSecond).c_str());
}


void TemporalAntiAliasingExample::checkAppInputs() {
    _camera->processInput();
}

void TemporalAntiAliasingExample::cleanup() {
    _loader->stop();
    AppContext::shutdown();
}

void TemporalAntiAliasingExample::onPause() {
    VulkanBaseApp::onPause();
}

void TemporalAntiAliasingExample::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto indexType8 = findExtension<VkPhysicalDeviceIndexTypeUint8FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, deviceCreateNextChain);
    indexType8->indexTypeUint8 = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;
    dsFeatures->extendedDynamicState3ColorBlendEnable = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void TemporalAntiAliasingExample::initBindlessDescriptor() {
    _bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
}

void TemporalAntiAliasingExample::newFrame() {
    const auto w = swapChain.width<float>();
    const auto h = swapChain.height<float>();


    if(options.jitterEnabled) {
        jitterValue = -1.f + 2.f * jitter.nextSample();
        jitterValue /= glm::vec2(width, height);

        _camera->newFrame();
        _camera->jitter(jitterValue.x, jitterValue.y);

    }else {
        jitterValue = glm::vec2(0);
    }

    taa->newFrame();

    auto camera = _camera->cam();
}

void TemporalAntiAliasingExample::endFrame() {
    taa->endFrame();
    jitter.sampler.type = static_cast<SamplerType>(options.samplerType);
    jitter.period(options.jitterPeriod);

    if(options.dirty) {
        taa->settings().historySamplingFilter = to<taa::HistorySamplingFilter>(options.filter);
        taa->settings().subSampleFilter = to<taa::SubSampleFilter>(options.sub_sample_filter);
        taa->settings().historyConstraint = to<taa::HistoryConstraint>(options.history_constraint);
        taa->settings().enableTemporalFiltering = options.temporal_filtering;
        taa->settings().enableInverseLuminanceFiltering = options.inverse_luminance_filtering;
        taa->settings().enableLuminanceDifferenceFiltering = options.luminance_difference_filtering;
        taa->settings().fullTaa = !options.simple;
        invalidateSwapChain();
        options.dirty = false;
    }

}

void TemporalAntiAliasingExample::applyTaa(VkCommandBuffer commandBuffer) {
    if(!options.taaEnabled) return;
    taa->exec(commandBuffer);
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.numGraphicsQueues = 2;
        settings.vSync = true;
        settings.enabledFeatures.wideLines = true;
        settings.depthTest = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);

        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = TemporalAntiAliasingExample{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}