#include "OminidirectionalShadowMapExample.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"
#include "AppContext.hpp"

OminidirectionalShadowMapExample::OminidirectionalShadowMapExample(const Settings& settings) : VulkanBaseApp("Omini Directional Shadow maps", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("data/models");
    fileManager().addSearchPathFront("ominidirectional_shadow_map");
    fileManager().addSearchPathFront("ominidirectional_shadow_map/data");
    fileManager().addSearchPathFront("ominidirectional_shadow_map/spv");
    fileManager().addSearchPathFront("ominidirectional_shadow_map/models");
    fileManager().addSearchPathFront("ominidirectional_shadow_map/textures");
}

void OminidirectionalShadowMapExample::initApp() {
    initBindlessDescriptor();
    initLoader();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createCommandPool();
    loadModel();
    initFloor();
    initCamera();
    createDescriptorSetLayouts();
    initLights();
    updateDescriptorSets();
    initShadowMap();
    createPipelineCache();
    createRenderPipeline();
}

void OminidirectionalShadowMapExample::loadModel() {
    // "leaving_room/living_room.obj"
//    m_model = m_loader->load(resource("leaving_room/living_room.obj"));
}

void OminidirectionalShadowMapExample::initLights() {
    glm::mat4 positionLight = glm::translate(glm::mat4{1}, {0, 2, 0});
    glm::mat4 model = positionLight; // * glm::scale(glm::mat4{1}, glm::vec3(0.2));
    auto sphere = primitives::sphere(100, 100, 1, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    light.vertices = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    light.indices = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void OminidirectionalShadowMapExample::initLoader() {
    m_loader = std::make_unique<asyncml::Loader>( &device, 32);
    m_loader->start();
}

void OminidirectionalShadowMapExample::initFloor() {
    m_floor = Floor{ device, *prototypes, {}, FileManager::resource("osm_floor.frag.spv"), { *m_bindLessDescriptor.descriptorSetLayout }};
    m_floor.init();
}

void OminidirectionalShadowMapExample::initBindlessDescriptor() {
    m_bindLessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet(TextureIDs::COUNT);
}

void OminidirectionalShadowMapExample::initShadowMap() {

    auto initializeTexture = [&]{
        auto format = VK_FORMAT_D16_UNORM;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = { _shadow.size, _shadow.size, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 6;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        auto& device = AppContext::device();
        _shadow.depth.image = device.createImage(imageInfo);

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        _shadow.depth.sampler = device.createSampler(samplerInfo);

        VkImageSubresourceRange resourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 6};
        _shadow.depth.imageView = _shadow.depth.image.createView(format, VK_IMAGE_VIEW_TYPE_CUBE, resourceRange);
//        for(auto i = 0; i < 6; i++) {
//            resourceRange.baseArrayLayer = i;
//            resourceRange.layerCount = 1;
//            _shadow.views[i] = _shadow.depth.image.createView(format, VK_IMAGE_VIEW_TYPE_2D, resourceRange);
//        }

        _shadow.depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
        _shadow.depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, resourceRange);
    };

    auto initLightSpaceData = [&] {
        updateShadowProjection(light.position);
        _shadow.cameras.gpu = device.createDeviceLocalBuffer(_shadow.cameras.cpu.data(), BYTE_SIZE(_shadow.cameras.cpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    };


    auto createDescriptorSet = [&]{
        _shadow.descriptorSetLayout =
            device.descriptorSetLayoutBuilder()
                .name("omini_shadow_map_set_layout")
                .binding(0)
                    .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    .descriptorCount(1)
                    .shaderStages(VK_SHADER_STAGE_VERTEX_BIT)
                .createLayout();
    };

    auto updateDescriptorSet = [&]{
        _shadow.descriptorSet = descriptorPool.allocate({ _shadow.descriptorSetLayout }).front();
        auto writes = initializers::writeDescriptorSets<1>();
        writes[0].dstSet = _shadow.descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        VkDescriptorBufferInfo info{ _shadow.cameras.gpu, 0, VK_WHOLE_SIZE };
        writes[0].pBufferInfo = &info;

        device.updateDescriptorSets(writes);

    };

    auto createPipelines = [&]{
        auto builder = device.graphicsPipelineBuilder();
        _shadow.pipeline.pipeline =
            builder
                .allowDerivatives()
                .shaderStage()
                    .vertexShader(FileManager::resource("omini_shadow_map.vert.spv"))
                    .fragmentShader(FileManager::resource("omini_shadow_map.frag.spv"))
                .vertexInputState()
                    .addVertexBindingDescriptions(Vertex::bindingDisc())
                    .addVertexAttributeDescriptions(Vertex::attributeDisc())
                .inputAssemblyState()
                    .triangles()
                .viewportState()
                    .viewport()
                        .origin(0, 0)
                        .dimension(_shadow.size, _shadow.size)
                        .minDepth(0)
                        .maxDepth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(_shadow.size, _shadow.size)
                    .add()
                .rasterizationState()
                    .cullFrontFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLessOrEqual()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .dynamicState()
                    .cullMode()
                    .primitiveTopology()
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_shadow.constants))
                    .addDescriptorSetLayout(_shadow.descriptorSetLayout)
                .dynamicRenderPass()
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                    .viewMask(0b00111111)
                .subpass(0)
                .name("point_shadow_map")
            .build(_shadow.pipeline.layout);

        _shadow.lamp.pipeline =
            builder
                .basePipeline(_shadow.pipeline.pipeline)
                .shaderStage()
                    .vertexShader(resource("lamp_shadow_map.vert.spv"))
                    .fragmentShader(resource("lamp_shadow_map.frag.spv"))
                .name("lamp_point_shadow_map")
            .build(_shadow.lamp.layout);

    };

    initializeTexture();
    initLightSpaceData();
    createDescriptorSet();
    updateDescriptorSet();
    createPipelines();

    m_bindLessDescriptor.update({ &_shadow.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0});
}

void OminidirectionalShadowMapExample::updateShadowProjection(const glm::vec3 &lightPosition) {
    _shadow.constants.lightPosition = lightPosition;

    _shadow.constants.farPlane = 1000.f;
    _shadow.constants.projection = vkn::perspective(glm::half_pi<float>(), 1.0f, 0.1f, _shadow.constants.farPlane);
    _shadow.cameras.cpu[0] = vkn::perspective(glm::half_pi<float>(), 1.0f, 0.1f, _shadow.constants.farPlane);
    for(auto i = 0; i < 6; ++i) {
        const auto& face = _shadow.Faces[i];
        auto view = glm::lookAt(lightPosition, lightPosition + face.direction, face.up);
        _shadow.cameras.cpu[i + 1] = view;
    }
}

void OminidirectionalShadowMapExample::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    glm::vec3 bmin{-2.582, 0.024, -0.425};
//    glm::vec3 bmax{3.148, 3.171, 8.174};
//
//    auto target = (bmin + bmax) * 0.5f;
    auto position = light.position + glm::vec3(0, 0, 1);
    camera->lookAt(position, {0, 0.5, 0}, {0, 1, 0});
}


void OminidirectionalShadowMapExample::createDescriptorPool() {
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

void OminidirectionalShadowMapExample::createDescriptorSetLayouts() {
//    m_meshSetLayout =
//        device.descriptorSetLayoutBuilder()
//            .name("mesh_descriptor_set_layout")
//            .binding(0)
//                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
//                .descriptorCount(m_model->numMeshes())
//                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
//            .binding(1)
//                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
//                .descriptorCount(m_model->numMaterials())
//                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
//            .createLayout();
}

void OminidirectionalShadowMapExample::updateDescriptorSets(){
//    auto sets = descriptorPool.allocate({ m_meshSetLayout });
//
//    m_meshDescriptorSet = sets[0];
//    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("mesh_descriptor_set", m_meshDescriptorSet);
//
//    updateMeshDescriptorSets();

}

void OminidirectionalShadowMapExample::updateMeshDescriptorSets() {
//    auto writes = initializers::writeDescriptorSets();
//    writes.resize(BindLessDescriptorPlugin::MaxDescriptorResources);
//
//    const auto numMeshes = m_model->numMeshes();
//    const VkDeviceSize meshDataSize = sizeof(asyncml::MeshData);
//    std::vector<VkDescriptorBufferInfo> meshInfos;
//    for(auto i = 0; i < numMeshes; i++) {
//        const VkDeviceSize offset = i * meshDataSize;
//        meshInfos.push_back({m_model->meshBuffer, offset, meshDataSize  });
//    }
//
//    writes.resize(2);
//    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//    writes[0].dstSet = m_meshDescriptorSet;
//    writes[0].dstBinding = 0;
//    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
//    writes[0].dstArrayElement = 0;
//    writes[0].descriptorCount = numMeshes;
//    writes[0].pBufferInfo = meshInfos.data();
//
//    const auto numMaterials = m_model->numMaterials();
//    const VkDeviceSize materialDataSize = sizeof(asyncml::MaterialData);
//    std::vector<VkDescriptorBufferInfo> materialInfos;
//    for(auto i = 0; i < numMaterials; i++){
//        materialInfos.push_back({m_model->materialBuffer, i * materialDataSize, materialDataSize});
//    }
//    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//    writes[1].dstSet = m_meshDescriptorSet;
//    writes[1].dstBinding = 1;
//    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
//    writes[1].dstArrayElement = 0;
//    writes[1].descriptorCount = numMaterials;
//    writes[1].pBufferInfo = materialInfos.data();
//
//    device.updateDescriptorSets(writes);
}

void OminidirectionalShadowMapExample::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OminidirectionalShadowMapExample::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OminidirectionalShadowMapExample::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
//        render.pipeline =
//            builder
//                .shaderStage()
//                    .vertexShader(resource("render.vert.spv"))
//                    .fragmentShader(resource("render.frag.spv"))
//                .layout()
//                    .addDescriptorSetLayout(m_meshSetLayout)
//                    .addDescriptorSetLayout(*m_bindLessDescriptor.descriptorSetLayout)
//                .name("render")
//                .build(render.layout);

        lamp.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("lamp.vert.spv"))
                    .fragmentShader(resource("lamp.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                .name("lamp")
            .build(lamp.layout);
    //    @formatter:on
}


void OminidirectionalShadowMapExample::onSwapChainDispose() {
    dispose(render.pipeline);
}

void OminidirectionalShadowMapExample::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *OminidirectionalShadowMapExample::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

    m_floor.render(commandBuffer, *camera, { m_bindLessDescriptor.descriptorSet });
    renderLights(commandBuffer);
//    renderModel(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OminidirectionalShadowMapExample::captureShadow(VkCommandBuffer commandBuffer) {
    static const auto size = _shadow.size;
    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.flags = 0;
    info.renderArea = {{0, 0}, {size, size}};
    info.layerCount = 6;
    info.viewMask = 0b00111111;

    VkRenderingAttachmentInfo attachmentInfo{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    attachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    attachmentInfo.resolveMode = VK_RESOLVE_MODE_NONE;
    attachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentInfo.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentInfo.clearValue.depthStencil = {_shadow.constants.farPlane, 0u};
    attachmentInfo.imageView = _shadow.depth.imageView.handle;

    info.pDepthAttachment = &attachmentInfo;

    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = _shadow.descriptorSet;

    vkCmdBeginRendering(commandBuffer, &info);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadow.lamp.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadow.lamp.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_NONE);
    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    VkDeviceSize offset = 0;
    glm::mat4 positionLight = glm::translate(glm::mat4{1}, light.position);
    _shadow.constants.model = positionLight * glm::scale(glm::mat4{1}, glm::vec3(0.2));

    vkCmdPushConstants(commandBuffer, _shadow.lamp.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(_shadow.constants), &_shadow.constants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, light.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, light.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, light.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

void OminidirectionalShadowMapExample::renderModel(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = m_meshDescriptorSet;
    sets[1] = m_bindLessDescriptor.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);

    camera->push(commandBuffer, render.layout);

    VkDeviceSize offset = 0;
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_model->vertexBuffer, &offset);
//    vkCmdBindIndexBuffer(commandBuffer, m_model->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
//    vkCmdDrawIndexedIndirect(commandBuffer, m_model->draw.gpu, 0, m_model->draw.count, sizeof(VkDrawIndexedIndirectCommand));
}

void OminidirectionalShadowMapExample::renderLights(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;

    glm::mat4 positionLight = glm::translate(glm::mat4{1}, light.position);
    glm::mat4 model = positionLight* glm::scale(glm::mat4{1}, glm::vec3(0.1));
    AppContext::renderFlat(commandBuffer, *camera, [&] {
            camera->push(commandBuffer, lamp.layout, model);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, light.vertices, &offset);
            vkCmdBindIndexBuffer(commandBuffer, light.indices, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, light.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
    });

    glm::mat4 positionLamp = glm::translate(glm::mat4{1}, lamp.position);
    model = positionLamp * glm::scale(glm::mat4{1}, glm::vec3(0.2));
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, lamp.pipeline.handle);
    camera->push(commandBuffer, lamp.layout, model);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, light.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, light.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, light.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void OminidirectionalShadowMapExample::update(float time) {
    camera->update(time);
    auto cam = camera->cam();

    static float elapsedTime = 0;
    elapsedTime += time;

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        captureShadow(commandBuffer);
    });

    static constexpr auto _2PI = glm::two_pi<float>();
//    glfwSetWindowTitle(window, fmt::format("{} - offset {}", title, offset).c_str());


    light.position = lamp.position + glm::vec3(glm::sin(_2PI * elapsedTime), 0, glm::cos(_2PI * elapsedTime)) * 0.02f;
}

void OminidirectionalShadowMapExample::checkAppInputs() {
    camera->processInput();
}

void OminidirectionalShadowMapExample::cleanup() {
    m_loader->stop();
    AppContext::shutdown();
}

void OminidirectionalShadowMapExample::onPause() {
    VulkanBaseApp::onPause();
}

void OminidirectionalShadowMapExample::endFrame() {
//    m_model->updateDrawState(device, m_bindLessDescriptor);
}

void OminidirectionalShadowMapExample::beforeDeviceCreation() {
    auto maybeExt = findExtension<VkPhysicalDeviceVulkan11Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES, deviceCreateNextChain);
    if(maybeExt.has_value()) {
        auto ext = *maybeExt;
        ext->multiview = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan11Features  vulkan11Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
        vulkan11Features.multiview = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, vulkan11Features);
    }

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->shaderOutputLayer = VK_TRUE;
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.shaderOutputLayer = VK_TRUE;
        devFeatures12.scalarBlockLayout = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    };

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
        devFeatures13.value()->maintenance4 = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        devFeatures13.maintenance4 = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);

//        settings.deviceExtensions.push_back(VK_EXT_TOOLING_INFO_EXTENSION_NAME);
//        settings.validationLayers.push_back("VK_LAYER_LUNARG_api_dump");

        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = OminidirectionalShadowMapExample{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}