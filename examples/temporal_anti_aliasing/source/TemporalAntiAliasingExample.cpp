#include "TemporalAntiAliasingExample.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"
#include "AppContext.hpp"

TemporalAntiAliasingExample::TemporalAntiAliasingExample(const Settings& settings) : VulkanBaseApp("Temporal Anti-Aliasing", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/temporal_anti_aliasing");
    fileManager().addSearchPathFront("../../examples/temporal_anti_aliasing/data");
    fileManager().addSearchPathFront("../../examples/temporal_anti_aliasing/spv");
    fileManager().addSearchPathFront("../../examples/temporal_anti_aliasing/models");
    fileManager().addSearchPathFront("../../examples/temporal_anti_aliasing/textures");
    fileManager().addSearchPathFront("../../dependencies/glTF-Sample-Assets/Models");
}

void TemporalAntiAliasingExample::initApp() {
    initBindlessDescriptor();
    createDescriptorPool();
    AppContext::init(device, _descriptorPool, swapChain, renderPass);
    initLoader();
    initGpuData();
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
    createComputePipeline();
}

void TemporalAntiAliasingExample::initLoader() {
    _loader = std::make_unique<gltf::Loader>( &device, &_descriptorPool, &_bindlessDescriptor, 32);
    _loader->start();
}

void TemporalAntiAliasingExample::loadModel() {
    _model = _loader->load(resource("FlightHelmet/glTF/FlightHelmet.gltf"));
//    _model = _loader->load( &_bindlessDescriptor, resource("WaterBottle/glTF/WaterBottle.gltf"));
}

void TemporalAntiAliasingExample::initCamera() {
//    OrbitingCameraSettings cameraSettings;
////    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
//    cameraSettings.fieldOfView = 60.0f;
//    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
//
//    _camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);

    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    _camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    _camera->lookAt({1, 0, 0}, {-0.054, -0.130, -0.054}, {0, 1, 0});
}

void TemporalAntiAliasingExample::initGpuData() {
    _scene.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneData), "scene_constants");
    _scene.cpu = reinterpret_cast<SceneData*>(_scene.gpu.map());

    _taa.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(TaaConstants), "taa_constants");
    _taa.cpu = reinterpret_cast<TaaData*>(_taa.gpu.map());
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

    format = VK_FORMAT_R32G32B32A32_SFLOAT;
    imageInfo.format = format;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    _textures.color.image = device.createImage(imageInfo);

    resourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    _textures.color.imageView = _textures.color.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);

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


    _textures.history[0].image = device.createImage(imageInfo);
    _textures.history[0].imageView = _textures.history[0].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.history[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    _textures.history[1].image = device.createImage(imageInfo);
    _textures.history[1].imageView = _textures.history[1].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.history[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.format = format;
    _textures.velocity.image = device.createImage(imageInfo);
    _textures.velocity.imageView = _textures.velocity.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.velocity.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    _offscreenInfo = Offscreen::RenderInfo{
        .colorAttachments = {{ &_textures.color, VK_FORMAT_R32G32B32A32_SFLOAT}},
        .depthAttachment = { { &_textures.depth, VK_FORMAT_D16_UNORM} },
        .renderArea = { _textures.width, _textures.height }
    };
}

void TemporalAntiAliasingExample::initCanvas() {
    _canvas = Canvas{ this };
    _canvas.init();
    
    _displaySet = _descriptorPool.allocate( { _canvas.getDescriptorSetLayout() } ).front();
    auto writes = initializers::writeDescriptorSets();
    auto& write = writes[0];
    
    write.dstSet = _displaySet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    VkDescriptorImageInfo info{ _textures.color.sampler.handle, _textures.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    write.pImageInfo = &info;
    
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
                    .vertexShader(resource("floor.vert.spv"))
                    .fragmentShader(resource("floor.frag.spv"))
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
                    .addVertexBindingDescriptions(Vertex::bindingDisc())
                    .addVertexAttributeDescriptions(Vertex::attributeDisc())
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
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(_loader->descriptorSetLayout())
                    .addDescriptorSetLayout(*_bindlessDescriptor.descriptorSetLayout)
                .name("model_pipeline")
            .build(_render.model.layout);
    //    @formatter:on
}

void TemporalAntiAliasingExample::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    _compute.velocity.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = _compute.velocity.layout.handle;

    _compute.velocity.pipeline = device.createComputePipeline(computeCreateInfo, _pipelineCache.handle);
}


void TemporalAntiAliasingExample::onSwapChainDispose() {
    dispose(_render.model.pipeline);
    dispose(_render.ground.pipeline);
    dispose(_compute.velocity.pipeline);
}

void TemporalAntiAliasingExample::onSwapChainRecreation() {
    initTextures();
    initCanvas();
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
    _camera->perspective(swapChain.aspectRatio());
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
    vkCmdEndRenderPass(commandBuffer);

    offscreenRender(commandBuffer);

    vkEndCommandBuffer(commandBuffer);


    return &commandBuffer;
}

void TemporalAntiAliasingExample::offscreenRender(VkCommandBuffer commandBuffer) {
    static Offscreen offscreen;

    offscreen.render(commandBuffer, _offscreenInfo, [&]{
        renderGround(commandBuffer);
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
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = _model->descriptorSet;
    sets[1] = _bindlessDescriptor.descriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    _camera->push(commandBuffer, _render.model.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _model->vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, _model->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw.gpu, 0, _model->draw.count, sizeof(VkDrawIndexedIndirectCommand));

//    AppContext::renderSolid(commandBuffer, *_camera, [&]{
//        VkDeviceSize offset = 0;
//        vkCmdBindVertexBuffers(commandBuffer, 0, 1, _model->vertexBuffer, &offset);
//        vkCmdBindIndexBuffer(commandBuffer, _model->indexBuffer, 0, VK_INDEX_TYPE_UINT16);
//        vkCmdDrawIndexed(commandBuffer, _model->indexBuffer.sizeAs<uint16_t>(), 1, 0, 0, 0);
//    }, true);
}

void TemporalAntiAliasingExample::update(float time) {
    _camera->update(time);
    auto cam = _camera->cam();
}

void TemporalAntiAliasingExample::updateScene(float time) {
    const auto& cam = _camera->cam();
    _scene.cpu->current_view_projection = cam.proj * cam.view;
    _scene.cpu->inverse_current_view_projection = glm::inverse(cam.proj * cam.view);

    const auto pCam = _camera->previousCamera();
    _scene.cpu->previous_view_projection = pCam.proj * pCam.view;
    _scene.cpu->inverse_previous_view_projection = glm::inverse(pCam.proj * pCam.view);

    _scene.cpu->world_to_camera = cam.view;
    _scene.cpu->camera_position = glm::vec4(_camera->position(), 0);
    _scene.cpu->camera_direction = _camera->viewDir;
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

void TemporalAntiAliasingExample::endFrame() {
}

void TemporalAntiAliasingExample::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);
}

void TemporalAntiAliasingExample::initBindlessDescriptor() {
    _bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
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