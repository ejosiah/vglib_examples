#include "VolumetricIntegration.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

VolumetricIntegration::VolumetricIntegration(const Settings& settings) : VulkanBaseApp("Volumetric Integration", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("data");
    fileManager().addSearchPathFront("data/textures");
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("volumetric_integration");
    fileManager().addSearchPathFront("volumetric_integration/data");
    fileManager().addSearchPathFront("volumetric_integration/spv");
    fileManager().addSearchPathFront("volumetric_integration/models");
    fileManager().addSearchPathFront("volumetric_integration/textures");
}

void VolumetricIntegration::initApp() {
    createLights();
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initBindlessDescriptor();
    createGBuffer();
    initLoader();
    loadTextures();
    loadModel();
    initShadowMap();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void VolumetricIntegration::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.0001;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void VolumetricIntegration::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, RESERVED_TEXTURE_SLOTS);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void VolumetricIntegration::beforeDeviceCreation() {
    auto maybeExt = findExtension<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, deviceCreateNextChain);
    if(maybeExt.has_value()) {
        auto ext = *maybeExt;
        ext->multiview = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan11Features  vulkan11Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        vulkan11Features.multiview = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, vulkan11Features);
    }

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);

    if(devFeatures13.has_value()) {
        devFeatures13.value()->maintenance4 = VK_TRUE;
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.maintenance4 = VK_TRUE;
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void VolumetricIntegration::createDescriptorPool() {
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


void VolumetricIntegration::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void VolumetricIntegration::createDescriptorSetLayouts() {
    sceneLightDescriptorSetLayout =
            device.descriptorSetLayoutBuilder()
                .name("scene_lights")
                    .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_ALL)
                .createLayout();
}

void VolumetricIntegration::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { sceneLightDescriptorSetLayout });
    sceneLightsDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = sceneLightsDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo sLightInfo{ lightBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &sLightInfo;

    device.updateDescriptorSets(writes);
}

void VolumetricIntegration::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumetricIntegration::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VolumetricIntegration::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.gbuffer.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("g_buffer.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
                .rasterizationState()
                    .cullNone()
                .colorBlendState()
                    .attachments(4)
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout()
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(sceneLightDescriptorSetLayout)
                .name("gbuffer_capture")
                .build(render.gbuffer.layout);


        render.forward.pipeline =
                prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
                .rasterizationState()
                    .cullNone()
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
                .layout()
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(sceneLightDescriptorSetLayout)
                .name("forward_render")
                .build(render.forward.layout);

        render.eval_lighting.pipeline =
                prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("clipspace.vert.spv"))
                    .fragmentShader(resource("evaluate_light.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_FRAGMENT_BIT))
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(sceneLightDescriptorSetLayout)
                .name("eval_lighting")
                .build(render.eval_lighting.layout);

        render.ray_march.pipeline =
                prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("clipspace.vert.spv"))
                    .fragmentShader(resource("raymarch_fog.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fogConstants))
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(sceneLightDescriptorSetLayout)
                .name("ray_march")
                .build(render.ray_march.layout);

        render.light.pipeline =
                prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("light.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addDescriptorSetLayout(sceneLightDescriptorSetLayout)
                .name("light_render")
                .build(render.light.layout);
    //    @formatter:on
}


void VolumetricIntegration::onSwapChainDispose() {
    dispose(render.gbuffer.pipeline);
}

void VolumetricIntegration::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *VolumetricIntegration::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    renderSceneToShadowMap(commandBuffer);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 0, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    if(renderMode == RenderMode::Deferred) {
        evaluateLighting(commandBuffer);
//        rayMarch(commandBuffer);
    }else {
        renderScene(commandBuffer, render.forward);
    }
    renderLight(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumetricIntegration::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    fogConstants.time += time;
    shadowMap.update(lights[0].position);

    setTitle(fmt::format("{}, FPS - {}, camera - {}", title, framePerSecond, camera->position()));
}

void VolumetricIntegration::checkAppInputs() {
    camera->processInput();
}

void VolumetricIntegration::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void VolumetricIntegration::onPause() {
    VulkanBaseApp::onPause();
}

void VolumetricIntegration::loadModel() {
    model = loader->loadGltf(resource("Sponza/glTF/Sponza.gltf"));
    model->sync();
}

void VolumetricIntegration::renderScene(VkCommandBuffer commandBuffer, const RenderPipeline& pipeline) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = sceneLightsDescriptorSet;

    VkDeviceSize offset = 0;
    camera->push(commandBuffer, pipeline.layout);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model->vertices, &offset);

//    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &blendingEnabled);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u16.handle, 0, VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u16.handle, 0, model->draw.u16.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u32.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u32.handle, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u32.handle, 0, model->draw.u32.count, sizeof(VkDrawIndexedIndirectCommand));

    sets[0] = model->meshDescriptorSet.u8.handle;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindIndexBuffer(commandBuffer, model->indices.u8.handle, 0, VK_INDEX_TYPE_UINT8_EXT);
    vkCmdDrawIndexedIndirect(commandBuffer, model->draw.u8.handle, 0, model->draw.u8.count, sizeof(VkDrawIndexedIndirectCommand));

}

void VolumetricIntegration::evaluateLighting(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = sceneLightsDescriptorSet;

    VkDeviceSize offset = 0;
    camera->push(commandBuffer, render.eval_lighting.layout, VK_SHADER_STAGE_FRAGMENT_BIT);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.eval_lighting.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.eval_lighting.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void VolumetricIntegration::rayMarch(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = sceneLightsDescriptorSet;

    vkCmdPushConstants(commandBuffer, render.ray_march.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(fogConstants), &fogConstants);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ray_march.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ray_march.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}


void VolumetricIntegration::renderLight(VkCommandBuffer commandBuffer) {
    if(!showLight) return;
    VkDeviceSize offset = 0;
    camera->push(commandBuffer, render.light.layout);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.light.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.light.layout.handle, 0, 1, &sceneLightsDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, lightObj.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, lightObj.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, lightObj.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void VolumetricIntegration::createLights() {
    std::vector<gltf::Light> aloc(2);
    aloc[0].position = glm::vec3(0, 2, 0);
    aloc[0].range = 10;
    aloc[0].intensity = 600;
    aloc[0].color = {1, 0.9, 0.5};
    aloc[0].type = to<int>(gltf::LightType::POINT);
    aloc[0].shadowMapIndex = 6;

    aloc[1].position = glm::vec3(0);
    aloc[1].direction = glm::vec3(1);
    aloc[1].range = 10;
    aloc[1].intensity = 10;
    aloc[1].type = to<int>(gltf::LightType::DIRECTIONAL);

    lightBuffer = device.createCpuVisibleBuffer(aloc.data(), BYTE_SIZE(aloc), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    lights = lightBuffer.span<gltf::Light>();

    auto sphere = primitives::sphere(10, 10, 0.1, glm::mat4{1}, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    lightObj.vertices = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    lightObj.indexes = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void VolumetricIntegration::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::SetNextWindowBgAlpha(0.8);
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({0, 0});
    if(ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::SliderFloat("x", &lights[0].position.x, -10, 10);
        ImGui::SliderFloat("y", &lights[0].position.y, -10, 10);
        ImGui::SliderFloat("z", &lights[0].position.z, -10, 10);
        ImGui::SliderFloat("Intensity", &lights[0].intensity, 10, 600);
        ImGui::SliderFloat("Range", &lights[0].range, 1, 20);
        ImGui::Checkbox("enable shadow", &shadow);
        ImGui::Checkbox("Show light marker", &showLight);
    }

    if(ImGui::CollapsingHeader("rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
        static int mode = to<int>(renderMode);
        ImGui::Text("Render Mode: ");
        ImGui::Indent(16);
        ImGui::RadioButton("forward", &mode, to<int>(RenderMode::Forward));
        ImGui::SameLine();
        ImGui::RadioButton("deferred", &mode, to<int>(RenderMode::Deferred));
        ImGui::Indent(-16);
        renderMode = to<RenderMode>(mode);
    }

    if(ImGui::CollapsingHeader("Fog", ImGuiTreeNodeFlags_DefaultOpen)) {
        static auto addNoise = bool(fogConstants.fog_noise);
        static auto fogStrength = fogConstants.fog_strength;
        static auto enableVolumeShadow = bool(fogConstants.enable_volume_shadow);
        static auto useImprovedIntegration = bool(fogConstants.use_improved_integration);
        static auto updateTransmissionFirst = bool(fogConstants.update_transmission_first);
        static auto heightFog = bool(fogConstants.heightFog);
        static auto boxFog = bool(fogConstants.boxFog);
        static auto constantFog = bool(fogConstants.constantFog);

        ImGui::Checkbox("Fog Noise", &addNoise);
        ImGui::Checkbox("Enable volume shadow", &enableVolumeShadow);
        ImGui::Checkbox("Use Improved Integration", &useImprovedIntegration);
        ImGui::Checkbox("constant fog", &constantFog);
        ImGui::Checkbox("height fog", &heightFog);
        ImGui::Checkbox("box fog", &boxFog);

        if(!useImprovedIntegration) {
            ImGui::Checkbox("Update transmission first", &updateTransmissionFirst);
        }

        ImGui::SliderFloat("Fog Strength", &fogStrength, 0.f, 10.f);
        ImGui::SliderFloat("Step scale", &fogConstants.stepScale, 0.1, 1.0);

        fogConstants.fog_noise = addNoise;
        fogConstants.fog_strength = fogStrength;
        fogConstants.enable_volume_shadow = enableVolumeShadow;
        fogConstants.use_improved_integration = useImprovedIntegration;
        fogConstants.update_transmission_first = updateTransmissionFirst;
        fogConstants.heightFog = heightFog;
        fogConstants.boxFog = boxFog;
        fogConstants.constantFog = constantFog;
    }

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VolumetricIntegration::createGBuffer() {
    textures::create(device, gbuffer.position, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, gbuffer.normal, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, gbuffer.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, gbuffer.metalRoughnessAmb, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);
    textures::create(device, gbuffer.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);

//    std::vector<VkImageMemoryBarrier2> barriers(5, { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 });
//
//    for(auto& barrier : barriers) {
//        barrier.srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
//        barrier.srcAccessMask = VK_ACCESS_NONE;
//        barrier.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
//        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
//        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
//        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
//        barrier.subresourceRange.baseMipLevel = 0;
//        barrier.subresourceRange.levelCount = 1;
//        barrier.subresourceRange.baseArrayLayer = 0;
//        barrier.subresourceRange.layerCount = 1;
//    }
//
//    barriers[0].image = gbuffer.position.image;
//    barriers[1].image = gbuffer.normal.image;
//    barriers[2].image = gbuffer.color.image;
//    barriers[2].image = gbuffer.metalRoughnessAmb.image;
//
//    barriers[4].image = gbuffer.depth.image;
//    barriers[4].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
//
//    VkDependencyInfo info {
//          .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
//          .imageMemoryBarrierCount = COUNT(barriers),
//          .pImageMemoryBarriers = barriers.data()
//    };
//
//    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
//        vkCmdPipelineBarrier2(commandBuffer, &info);
//    });

    gbuffer.renderInfo = Offscreen::RenderInfo{
        .colorAttachments = {
                {&gbuffer.position, VK_FORMAT_R32G32B32A32_SFLOAT},
                {&gbuffer.normal, VK_FORMAT_R32G32B32A32_SFLOAT},
                {&gbuffer.color, VK_FORMAT_R32G32B32A32_SFLOAT},
                {&gbuffer.metalRoughnessAmb, VK_FORMAT_R32G32B32A32_SFLOAT},
        },
        .depthAttachment = { { &gbuffer.depth, VK_FORMAT_D16_UNORM}},
        .renderArea = { width, height }
    };

    bindlessDescriptor.update({ &gbuffer.position, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0 });
    bindlessDescriptor.update({ &gbuffer.normal, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 });
    bindlessDescriptor.update({ &gbuffer.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 });
    bindlessDescriptor.update({ &gbuffer.metalRoughnessAmb, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 });
    bindlessDescriptor.update({ &gbuffer.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 });
}

void VolumetricIntegration::endFrame() {
    updateGBuffer();
    fogConstants.model = camera->camera.model;
    fogConstants.view = camera->camera.view;
    fogConstants.projection = camera->camera.proj;
    fogConstants.camera_position = camera->position();

    int nexFrame = (currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
    bindlessDescriptor.update({ &shadowMap.shadowMap(nexFrame), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 });

}

void VolumetricIntegration::updateGBuffer() {
    if(renderMode != RenderMode::Deferred) return;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        offscreen.render(commandBuffer, gbuffer.renderInfo, [&]{
            renderScene(commandBuffer, render.gbuffer);
        });
    });
}

void VolumetricIntegration::loadTextures() {
    grayNoise.format = VK_FORMAT_R8_SRGB;
    grayNoise.spec.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto status = loader->loadTexture(resource("gray_noise_small.png"), grayNoise);
    onIdle([this, status = std::move(status)]{ updateTextureBinding(status); });
}

void VolumetricIntegration::updateTextureBinding(std::shared_ptr<gltf::TextureUploadStatus> status) {
    if(status->success) {
        bindlessDescriptor.update({ status->texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 });
        spdlog::info("gray noise texture successfully updated to gpu");
    }else {
        onIdle([this, status = std::move(status)]{ updateTextureBinding(status); });
    }
}

void VolumetricIntegration::initShadowMap() {
    shadowMap = PointShadowMap{ device, descriptorPool, MAX_IN_FLIGHT_FRAMES, VK_FORMAT_D16_UNORM };
    shadowMap.init();
    bindlessDescriptor.update({ &shadowMap.shadowMap(0), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 });
}

void VolumetricIntegration::renderSceneToShadowMap(VkCommandBuffer commandBuffer) {
    if(!shadow) {
        clear(commandBuffer, shadowMap.shadowMap(currentFrame), glm::vec4(MAX_FLOAT), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return;
    }
    shadowMap.capture([this, commandBuffer](auto& layout) {
        model->render(commandBuffer, layout, 1);
    }, commandBuffer, currentFrame);
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width =  1440;
        settings.height = 1280;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = VolumetricIntegration{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}