#include "CascadeShadowMapDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

CascadeShadowMapDemo::CascadeShadowMapDemo(const Settings& settings) : VulkanBaseApp("Cascade shadow map", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("cascade_shadow_map");
    fileManager().addSearchPathFront("cascade_shadow_map/data");
    fileManager().addSearchPathFront("cascade_shadow_map/spv");
    fileManager().addSearchPathFront("cascade_shadow_map/models");
    fileManager().addSearchPathFront("cascade_shadow_map/textures");
}

void CascadeShadowMapDemo::initApp() {
    initCamera();
    createCube();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    loadModel();
    initShadowMaps();
    initUniforms();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void CascadeShadowMapDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
//    OrbitingCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.acceleration = glm::vec3(10);
    cameraSettings.velocity = glm::vec3(50.f);
    cameraSettings.zNear = 0.5;
    cameraSettings.zFar = 100;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    sceneCamera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    sceneCamera->lookAt({0, 1, 1}, glm::vec3(0, 1, 0), {0, 1, 0});
    sceneCamera->zoomDelta = 10;

    cameraSettings.zFar = 1000;
    debugCamera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera = sceneCamera.get();
}

void CascadeShadowMapDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void CascadeShadowMapDemo::beforeDeviceCreation() {
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
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
        devFeatures12.value()->shaderOutputLayer = VK_TRUE;
        devFeatures12.value()->shaderOutputViewportIndex = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        devFeatures12.shaderOutputLayer = VK_TRUE;
        devFeatures12.shaderOutputViewportIndex = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
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

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void CascadeShadowMapDemo::createDescriptorPool() {
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


void CascadeShadowMapDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void CascadeShadowMapDemo::createDescriptorSetLayouts() {
    lightDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("light_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();

}

void CascadeShadowMapDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ lightDescriptorSetLayout });
    lightDescriptorSet = sets[0];
    
    auto writes = initializers::writeDescriptorSets<3>();
    
    writes[0].dstSet = lightDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ ubo.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uboInfo;

    writes[1].dstSet = lightDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo shadowMatrixInfo{ shadowMap.cascadeViewProjection(), 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &shadowMatrixInfo;

    writes[2].dstSet = lightDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo splitDepthInfo{ splitDepthBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &splitDepthInfo;

    device.updateDescriptorSets(writes);
}

void CascadeShadowMapDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void CascadeShadowMapDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void CascadeShadowMapDemo::createRenderPipeline() {
    //    @formatter:off
    render.model.pipeline =
		prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("gltf.vert.spv"))
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
                .addDescriptorSetLayout(lightDescriptorSetLayout)
            .name("render")
        .build(render.model.layout);
    //    @formatter:off
    render.frustum.pipeline =
		prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("frustum.vert.spv"))
                .fragmentShader(resource("frustum.frag.spv"))
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
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(render.frustum.constants))
                .addDescriptorSetLayout(shadowMap.descriptorSetLayout())
            .name("frustum.render")
        .build(render.frustum.layout);

    render.camera.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("camera.vert.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .inputAssemblyState()
                .lines()
            .rasterizationState()
                .lineWidth(2.5)
            .depthStencilState()
            .name("camera_render")
        .build(render.camera.layout);
    //    @formatter:on
}


void CascadeShadowMapDemo::onSwapChainDispose() {
    dispose(render.model.pipeline);
}

void CascadeShadowMapDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *CascadeShadowMapDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    if(!freezeShadowMap) {
        shadowMap.capture([this, commandBuffer](auto &layout) {
            model->render(commandBuffer, layout, 1);
        }, commandBuffer, currentFrame);
    }

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 1, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if(!showShadowMap) {
        renderScene(commandBuffer, render.model.pipeline, render.model.layout);
        renderFrustum(commandBuffer);
        renderCamera(commandBuffer);
    }else{
        shadowMap.render(commandBuffer);
    }
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void CascadeShadowMapDemo::update(float time) {
    if (!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    static auto elapsedTime = 0.f;
    elapsedTime += time * 0.1;
    float angle = glm::radians(elapsedTime * 360.0f);
    float radius = 20.0f;
//    lightDirection = glm::normalize(glm::vec3(cos(angle) * radius, -radius, sin(angle) * radius));

    if (!freezeShadowMap) {
        shadowMap.splitLambda(splitLambda);
        shadowMap.update(*camera, lightDirection, splitDepth);
    }
}

void CascadeShadowMapDemo::checkAppInputs() {
    camera->processInput();
}

void CascadeShadowMapDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void CascadeShadowMapDemo::onPause() {
    VulkanBaseApp::onPause();
}

void CascadeShadowMapDemo::loadModel() {
    model = loader->loadGltf(resource("SereneGrassFieldwithTree/SereneGrassFieldwithTree.gltf"));
//    camera->updateModel(model->bounds.min, model->bounds.max);
}

void CascadeShadowMapDemo::renderScene(VkCommandBuffer commandBuffer, VulkanPipeline& pipeline, VulkanPipelineLayout& layout) {
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = model->meshDescriptorSet.u16.handle;
    sets[1] = model->materialDescriptorSet;
    sets[2] = bindlessDescriptor.descriptorSet;
    sets[3] = lightDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, layout);
    model->render(commandBuffer, layout);
}

void CascadeShadowMapDemo::renderFrustum(VkCommandBuffer commandBuffer) {
    if(!showFrustum) return;
    VkDeviceSize offset = 0;
    static const auto descriptorSet = shadowMap.descriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.frustum.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.frustum.layout.handle, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, render.frustum.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(render.frustum.constants), &render.frustum.constants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, cube.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void CascadeShadowMapDemo::renderUI(VkCommandBuffer commandBuffer) {
    static bool color_cascades = false;
    static bool use_pcf_filtering = false;
    static bool show_extents = false;

    ImGui::Begin("CSM");
    ImGui::SetWindowSize({0, 0});

    if(ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        freezePressed = ImGui::Checkbox("freeze shadow", &freezeShadowMap);

        if(!freezeShadowMap) {
            ImGui::SliderFloat("Split lambda", &splitLambda, 0.1, 1.0);
        }
        ImGui::Checkbox("Color cascades", &color_cascades);

        if (color_cascades) {
            ImGui::SameLine();
            ImGui::Checkbox("Show extents", &show_extents);
        }

        ImGui::Checkbox("PCF filtering", &use_pcf_filtering);
        ImGui::Checkbox("Show depth map", &showShadowMap);

        ImGui::Checkbox("Show frustum", &showFrustum);
        if(showFrustum) {
            ImGui::SameLine();
            ImGui::SliderInt("cascadeIndex", &render.frustum.constants.cascadeIndex, 0, shadowMap.cascadeCount() - 1);
        }
        ImGui::Checkbox("Show camera", &showCamera);

    }

    ImGui::End();

    ubo.cpu->colorCascades = color_cascades;
    ubo.cpu->usePCF = use_pcf_filtering;
    ubo.cpu->showExtents = show_extents;
    ubo.cpu->shadowOn = 1;

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void CascadeShadowMapDemo::initShadowMaps() {
    shadowMap = CascadeShadowMap{ device, descriptorPool, MAX_IN_FLIGHT_FRAMES,  depthBuffer.format};
    shadowMap.setRenderPass(renderPass, { width, height});
    shadowMap.init();
    bindlessDescriptor.update({ &shadowMap.shadowMap(0), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0 });
}

void CascadeShadowMapDemo::initUniforms() {
    auto uboData = UniformData{lightDirection, int(shadowMap.cascadeCount()), 0, 0, 0, 0};
    ubo.gpu = device.createCpuVisibleBuffer(&uboData, sizeof(uboData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    ubo.cpu = reinterpret_cast<UniformData*>(ubo.gpu.map());

    std::vector<float> memory(shadowMap.cascadeCount());
    splitDepthBuffer = device.createCpuVisibleBuffer(memory.data(), BYTE_SIZE(memory), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    splitDepth = splitDepthBuffer.span<float>();
}

void CascadeShadowMapDemo::endFrame() {
    if (!freezeShadowMap) {
        auto nextFrame = (currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
        bindlessDescriptor.update({&shadowMap.shadowMap(nextFrame), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0});
        ubo.cpu->lightDir = lightDirection;
    }
    render.frustum.constants.camera = camera->cam();
    if (freezePressed) {
        if (freezeShadowMap) {
            debugCamera->target = sceneCamera->target;
            debugCamera->position(sceneCamera->position());
            camera = debugCamera.get();
        } else {
            camera = sceneCamera.get();
        }
        freezePressed = false;
    }
    computeCameraBounds();
}

void CascadeShadowMapDemo::createCube() {
    glm::mat4 transform = glm::translate(glm::mat4{1}, {0, 0, 0.5});
    transform = glm::scale(transform, {1, 1, 0.5});

    auto primitive = primitives::cube({1, 1, 0, 1}, transform);
    cube.vertices = device.createDeviceLocalBuffer(primitive.vertices.data(), BYTE_SIZE(primitive.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indexes = device.createDeviceLocalBuffer(primitive.indices.data(), BYTE_SIZE(primitive.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    primitive = primitives::cubeOutline(glm::vec4(0, 0, 0, 1), transform);
    cameraBounds.vertices = device.createDeviceLocalBuffer(primitive.vertices.data(), BYTE_SIZE(primitive.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void CascadeShadowMapDemo::computeCameraBounds() {
    static glm::vec3 aabbMin, aabbMax;
    aabbMin = glm::vec3{MAX_FLOAT};
    aabbMax = glm::vec3{MIN_FLOAT};
    sceneCamera->extractAABB(aabbMin, aabbMax);

    auto dim = glm::ceil(aabbMin - aabbMax);

    const auto near = sceneCamera->near();
    const auto far = sceneCamera->far();
    const auto camPosOffset = glm::vec3(0, 0, -(far - near) * 0.5 - near);

    cameraBounds.aabb = glm::mat4{1};
    cameraBounds.aabb = glm::translate(cameraBounds.aabb, camPosOffset);
    cameraBounds.aabb = glm::scale(cameraBounds.aabb, dim);
    cameraBounds.aabb = glm::inverse(sceneCamera->camera.view) * cameraBounds.aabb;
    cameraBounds.transform = glm::inverse(sceneCamera->camera.proj * sceneCamera->camera.view);
}

void CascadeShadowMapDemo::renderCamera(VkCommandBuffer commandBuffer) {
    if(!showCamera) return;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.camera.pipeline.handle);
    camera->push(commandBuffer, render.camera.layout, cameraBounds.transform);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cameraBounds.vertices, &offset);
    vkCmdDraw(commandBuffer, cameraBounds.vertices.sizeAs<Vertex>(), 1, 0, 0);
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
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_SHADER_VIEWPORT_INDEX_LAYER_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = CascadeShadowMapDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}