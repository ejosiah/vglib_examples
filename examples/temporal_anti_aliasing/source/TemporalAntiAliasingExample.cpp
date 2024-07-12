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
    initJitter();
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
    _loader2 = std::make_unique<gltf::Loader>(&device, &_descriptorPool, &_bindlessDescriptor);
    _loader2->start();
}

void TemporalAntiAliasingExample::initJitter() {

}

void TemporalAntiAliasingExample::loadModel() {
//    _model2 = _loader2->load(resource("Sponza/glTF/Sponza.gltf"));
    _model2 = _loader2->load(resource("FlightHelmet/glTF/FlightHelmet.gltf"));
//    _model2 = _loader2->load(resource("ABeautifulGame/glTF/ABeautifulGame.gltf"));
    _model2->transform = glm::translate(glm::mat4{1}, -_model2->bounds.min);
//    _model2->sync();
}

void TemporalAntiAliasingExample::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
//    cameraSettings.velocity = glm::vec3(20);
//    cameraSettings.acceleration = glm::vec3(10);

    _camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);

    auto target = (_model2->bounds.min + _model2->bounds.max) * 0.5f;
    auto position = target - glm::vec3(0, 0, -1);

    _camera->lookAt(position, target, {0, 1, 0});
}

void TemporalAntiAliasingExample::initGpuData() {
    _scene.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneData), "scene_constants");
    _scene.cpu = reinterpret_cast<SceneData*>(_scene.gpu.map());
    _scene.cpu->color_buffer_index = ColorBindingIndex;
    _scene.cpu->depth_buffer_index = DepthBindingIndex;

    _taa.gpu = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(TaaConstants), "taa_constants");
    _taa.cpu = reinterpret_cast<TaaData*>(_taa.gpu.map());
    _taa.cpu->history_color_texture_index = HistoryBindingIndex;
    _taa.cpu->taa_output_texture_index = TaaOutputBindingIndex;
    _taa.cpu->velocity_texture_index = VelocityBindingIndex;

    auto outline = primitives::cubeOutline({1, 1, 0, 1});
    _modelPlaceHolder = device.createDeviceLocalBuffer(outline.vertices.data(), BYTE_SIZE(outline.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
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

    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    _textures.history[0].image = device.createImage(imageInfo);
    _textures.history[0].imageView = _textures.history[0].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.history[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    _textures.history[1].image = device.createImage(imageInfo);
    _textures.history[1].imageView = _textures.history[1].image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.history[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    format = VK_FORMAT_R16G16_SFLOAT;
    imageInfo.format = format;
    imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    _textures.velocity.image = device.createImage(imageInfo);
    _textures.velocity.imageView = _textures.velocity.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
    _textures.velocity.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, resourceRange);

    _offscreenInfo = Offscreen::RenderInfo{
        .colorAttachments = {{ &_textures.color, VK_FORMAT_R32G32B32A32_SFLOAT}},
        .depthAttachment = { { &_textures.depth, VK_FORMAT_D16_UNORM} },
        .renderArea = { _textures.width, _textures.height }
    };

    _bindlessDescriptor.update({ &_textures.history[0], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, HistoryBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.history[1], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TaaOutputBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.velocity, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VelocityBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ColorBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DepthBindingIndex, VK_IMAGE_LAYOUT_GENERAL });

    _bindlessDescriptor.update({ &_textures.history[0], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HistoryBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.history[1], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, TaaOutputBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
    _bindlessDescriptor.update({ &_textures.velocity, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VelocityBindingIndex, VK_IMAGE_LAYOUT_GENERAL });
}

void TemporalAntiAliasingExample::initCanvas() {
    _canvas = Canvas{ this };
    _canvas.init();
    
    auto sets = _descriptorPool.allocate( { _canvas.getDescriptorSetLayout(), _canvas.getDescriptorSetLayout(), _canvas.getDescriptorSetLayout() } );
    _colorDisplaySet = sets[0];
    _historyDisplaySet[0] = sets[1];
    _historyDisplaySet[1] = sets[2];
    auto writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = _colorDisplaySet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo info{ _textures.color.sampler.handle, _textures.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &info;

    writes[1].dstSet = _historyDisplaySet[0];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo history0Info{ _textures.color.sampler.handle, _textures.history[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &history0Info;

    writes[2].dstSet = _historyDisplaySet[1];
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo history1Info{ _textures.color.sampler.handle, _textures.history[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &history1Info;
    
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
    _modelDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("model_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
                
}

void TemporalAntiAliasingExample::updateDescriptorSets(){
    auto sets = _descriptorPool.allocate({_modelDescriptorSetLayout });
    _modelDescriptorSet = sets[0];
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = _modelDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo sceneInfo{ _scene.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &sceneInfo;

    writes[1].dstSet = _modelDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo taaInfo{ _taa.gpu, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &taaInfo;

    device.updateDescriptorSets(writes);
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
                    .addDescriptorSetLayout(_loader2->descriptorSetLayout())
                    .addDescriptorSetLayout(_loader2->descriptorSetLayout())
                    .addDescriptorSetLayout(*_bindlessDescriptor.descriptorSetLayout)
                .name("model_pipeline")
            .build(_render.model.layout);

        _render.placeHolder.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
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

void TemporalAntiAliasingExample::createComputePipeline() {
    auto module = device.createShaderModule(resource("camera_motion.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    _compute.velocity.layout = device.createPipelineLayout({_modelDescriptorSetLayout, *_bindlessDescriptor.descriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = _compute.velocity.layout.handle;

    _compute.velocity.pipeline = device.createComputePipeline(computeCreateInfo, _pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("motion_vectors_layout", _compute.velocity.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("motion_vectors_pipeline", _compute.velocity.pipeline.handle);

    module = device.createShaderModule(resource("resolve.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    _compute.resolve.layout = device.createPipelineLayout({_modelDescriptorSetLayout, *_bindlessDescriptor.descriptorSetLayout });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = _compute.resolve.layout.handle;

    _compute.resolve.pipeline = device.createComputePipeline(computeCreateInfo, _pipelineCache.handle);

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("taa_resolve_layout", _compute.resolve.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("taa_resolve_pipeline", _compute.resolve.pipeline.handle);

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
//    sets[0] = _model->mesh16descriptorSet;
//    sets[1] = _model->materialDescriptorSet;
//    sets[2] = _bindlessDescriptor.descriptorSet;
//
//    VkDeviceSize offset = 0;
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.pipeline.handle);
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
//    _camera->push(commandBuffer, _render.model.layout);
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _model->vertexBuffer, &offset);
//
//    vkCmdBindIndexBuffer(commandBuffer, _model->indexBufferUint16, 0, VK_INDEX_TYPE_UINT16);
//    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw_16.gpu, 0, _model->draw_16.count, sizeof(VkDrawIndexedIndirectCommand));
//
//    sets[0] = _model->mesh32descriptorSet;
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdBindIndexBuffer(commandBuffer, _model->indexBufferUint32, 0, VK_INDEX_TYPE_UINT32);
//    vkCmdDrawIndexedIndirect(commandBuffer, _model->draw_32.gpu, 0, _model->draw_32.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = _model2->meshDescriptorSet.u16.handle;
    sets[1] = _model2->materialDescriptorSet;
    sets[2] = _bindlessDescriptor.descriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    _camera->push(commandBuffer, _render.model.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _model2->vertices, &offset);

    vkCmdBindIndexBuffer(commandBuffer, _model2->indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, _model2->draw.u16.handle, 0, _model2->draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = _model2->meshDescriptorSet.u32.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.model.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, _model2->indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, _model2->draw.u32.handle, 0, _model2->draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));

}

void TemporalAntiAliasingExample::renderPlaceHolders(VkCommandBuffer commandBuffer) {

    static glm::mat4 model{1};
    static VkDeviceSize offset{0};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _render.placeHolder.pipeline.handle);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, _modelPlaceHolder, &offset);

    for(auto& transform : _model2->placeHolders) {
        model = transform;
        _camera->push(commandBuffer, _render.placeHolder.layout, model);
        vkCmdDraw(commandBuffer, _modelPlaceHolder.sizeAs<Vertex>(), 1, 0, 0);
    }

}

void TemporalAntiAliasingExample::renderUI(VkCommandBuffer commandBuffer) {

    ImGui::Begin("TAA");
    ImGui::SetWindowSize({0, 0});
    ImGui::Checkbox("Enable", &options.taaEnabled);
    ImGui::Checkbox("Jittering", &options.jitterEnabled);
    ImGui::Combo("Sampler", &options.samplerType, options.samplers.data(), options.samplers.size());
    ImGui::SliderInt("period", &options.jitterPeriod, 2, 16);

    if(!options.simple) {
        ImGui::Combo("Filter", &options.filter, options.filters.data(), options.filters.size());
        ImGui::Combo("Sub-Sample Filter", &options.sub_sample_filter, options.subSampleFilters.data(), options.subSampleFilters.size());
        ImGui::Combo("History Constraint", &options.history_constraint, options.historyConstraints.data(), options.historyConstraints.size());

        ImGui::Text("Blending Weights filtering:");
        ImGui::Indent(16);
        ImGui::Checkbox("Temporal", &options.temporal_filtering);
        ImGui::SameLine();
        ImGui::Checkbox("Inverse Luminance", &options.inverse_luminance_filtering);
        ImGui::SameLine();
        ImGui::Checkbox("Luminance Difference", &options.luminance_difference_filtering);
        ImGui::Indent(-16);
    }

    ImGui::Checkbox("Simple TAA", &options.simple);

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void TemporalAntiAliasingExample::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        _camera->update(time);
    }

    glfwSetWindowTitle(window, fmt::format("{}, fps: {}", title, framePerSecond).c_str());
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
    _loader2->stop();
    AppContext::shutdown();
}

void TemporalAntiAliasingExample::onPause() {
    VulkanBaseApp::onPause();
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
    _bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5);
    _bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3);
}

void TemporalAntiAliasingExample::newFrame() {
    _displaySet = options.taaEnabled ? _historyDisplaySet[_taa.cpu->taa_output_texture_index] : _colorDisplaySet;

    const auto w = swapChain.width<float>();
    const auto h = swapChain.height<float>();
    _scene.cpu->resolution = glm::vec2(w, h);


    if(options.jitterEnabled) {
        auto sample = -1.f + 2.f * jitter.nextSample();
        sample /= _scene.cpu->resolution;

        _camera->newFrame();
        _camera->jitter(sample.x, sample.y);

        _scene.cpu->previous_jitter_xy = _scene.cpu->jitter_xy;
        _scene.cpu->jitter_xy = sample;
    }else {
        _scene.cpu->previous_jitter_xy = _scene.cpu->jitter_xy = glm::vec2(0);
    }

    auto camera = _camera->cam();
    _scene.cpu->current_view_projection = camera.proj * camera.view;
    _scene.cpu->inverse_current_view_projection = glm::inverse(camera.proj * camera.view);

    auto pCamera = _camera->previousCamera();
    _scene.cpu->previous_view_projection = pCamera.proj * pCamera.view;
    _scene.cpu->inverse_previous_view_projection = glm::inverse(pCamera.proj * pCamera.view);

    _scene.cpu->world_to_camera = camera.view;
    _scene.cpu->camera_position = glm::vec4(_camera->position(), 0);
    _scene.cpu->camera_direction = _camera->viewDir;
    _scene.cpu->current_frame++;
    _taa.cpu->taaSimple = int(options.simple);
    _taa.cpu->filter = options.filter;
    _taa.cpu->sub_sample_filter = options.sub_sample_filter;
    _taa.cpu->history_constraint = options.history_constraint;
    _taa.cpu->temporal_filtering = int(options.temporal_filtering);
    _taa.cpu->inverse_luminance_filtering = int(options.inverse_luminance_filtering);
    _taa.cpu->luminance_difference_filtering = int(options.luminance_difference_filtering);
    std::swap(_taa.cpu->taa_output_texture_index, _taa.cpu->history_color_texture_index);


}

void TemporalAntiAliasingExample::endFrame() {
//    static bool once = true;
//    if(_model->texturesLoaded() && once) {
//        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
//           for(auto& texture : _model->textures) {
//               textures::generateLOD(device, texture, texture.levels);
//           }
//        });
//        once = false;
//    }

    jitter.sampler.type = static_cast<SamplerType>(options.samplerType);
    jitter.period(options.jitterPeriod);

}

void TemporalAntiAliasingExample::applyTaa(VkCommandBuffer commandBuffer) {
    if(!options.taaEnabled) return;
    computeMotionVectors(commandBuffer);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.image = _textures.velocity.image;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier);
    taaResolve(commandBuffer);

    VkImageMemoryBarrier taaDisplayBarrier = barrier;
    taaDisplayBarrier.image = _textures.history[_taa.cpu->taa_output_texture_index].image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &taaDisplayBarrier);
}

void TemporalAntiAliasingExample::computeMotionVectors(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = _modelDescriptorSet;
    sets[1] = _bindlessDescriptor.descriptorSet;

    glm::uvec3 gc{ (swapChain.width() + 8)/8, (swapChain.height() + 8)/8, 1 };

    static VkClearColorValue clearColor{ 0.f, 0.f, 0.f, 0.f};
    vkCmdClearColorImage(commandBuffer, _textures.velocity.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _compute.velocity.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _compute.velocity.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
}

void TemporalAntiAliasingExample::taaResolve(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = _modelDescriptorSet;
    sets[1] = _bindlessDescriptor.descriptorSet;

    glm::uvec3 gc{ (swapChain.width() + 8)/8, (swapChain.height() + 8)/8, 1 };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _compute.resolve.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _compute.resolve.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
}

int main(){
    try{

        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
        settings.vSync = true;
        settings.enabledFeatures.wideLines = true;
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