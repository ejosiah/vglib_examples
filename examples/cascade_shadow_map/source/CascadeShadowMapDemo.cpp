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
//    FirstPersonSpectatorCameraSettings cameraSettings;
    OrbitingCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.fieldOfView = 60.0f;
//    cameraSettings.zNear = 0.1;
//    cameraSettings.zFar = 40;
//    cameraSettings.zFar = 100;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({10, 10, 10}, glm::vec3(0), {0, 1, 0});
    camera->zoomDelta = 10;
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
    render.pipeline =
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
        .build(render.layout);
    //    @formatter:on
}


void CascadeShadowMapDemo::onSwapChainDispose() {
    dispose(render.pipeline);
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

    // capture shadow
    shadowMap.capture([this, commandBuffer](auto& layout) {
        model->render(commandBuffer, layout, 1);
    }, commandBuffer, currentFrame);

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
        renderScene(commandBuffer, render.pipeline, render.layout);
    }else{
        shadowMap.render(commandBuffer);
    }
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void CascadeShadowMapDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    static auto elapsedTime = 0.f;
    elapsedTime += time * 0.1;
    float angle = glm::radians(elapsedTime * 360.0f);
    float radius = 20.0f;
//    lightDirection = glm::normalize(glm::vec3(cos(angle) * radius, -radius, sin(angle) * radius));
    shadowMap.splitLambda(splitLambda);
    shadowMap.update(*camera, lightDirection, splitDepth);
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
    camera->updateModel(model->bounds.min, model->bounds.max);
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

void CascadeShadowMapDemo::renderUI(VkCommandBuffer commandBuffer) {
    static bool color_cascades = false;
    static bool use_pcf_filtering = false;
    static bool show_extents = false;
    static bool shadow_on = true;

    ImGui::Begin("CSM");
    ImGui::SetWindowSize({0, 0});

    if(ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Shadow On", &shadow_on);

        if(shadow_on) {
            ImGui::SliderFloat("Split lambda", &splitLambda, 0.1, 1.0);
            ImGui::Checkbox("Color cascades", &color_cascades);

            if (color_cascades) {
                ImGui::SameLine();
                ImGui::Checkbox("Show extents", &show_extents);
            }

            ImGui::Checkbox("PCF filtering", &use_pcf_filtering);
        }
        ImGui::Checkbox("Show depth map", &showShadowMap);

    }

    ImGui::End();

    ubo.cpu->colorCascades = color_cascades;
    ubo.cpu->usePCF = use_pcf_filtering;
    ubo.cpu->showExtents = show_extents;
    ubo.cpu->shadowOn = shadow_on;

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void CascadeShadowMapDemo::initShadowMaps() {
    shadowMap = CascadeShadowMap{ device, descriptorPool, MAX_IN_FLIGHT_FRAMES,  depthBuffer.format, 1};
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
    auto nextFrame = (currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
    bindlessDescriptor.update({ &shadowMap.shadowMap(nextFrame), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0 });
    ubo.cpu->lightDir = lightDirection;
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