#include "OminidirectionalShadowMapDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

OminidirectionalShadowMapDemo::OminidirectionalShadowMapDemo(const Settings& settings) : VulkanBaseApp("Ominidirectional shadow map", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("data");
    fileManager().addSearchPathFront("data/textures");
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("data/models");
    fileManager().addSearchPathFront("ominidirectional_shadow_map2");
    fileManager().addSearchPathFront("ominidirectional_shadow_map2/data");
    fileManager().addSearchPathFront("ominidirectional_shadow_map2/spv");
    fileManager().addSearchPathFront("ominidirectional_shadow_map2/models");
    fileManager().addSearchPathFront("ominidirectional_shadow_map2/textures");
}

void OminidirectionalShadowMapDemo::initApp() {
    initCamera();
    createDescriptorPool();
    createLights();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initBindlessDescriptor();
    initLoader();
    loadModel();
    initShadowMap();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void OminidirectionalShadowMapDemo::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void OminidirectionalShadowMapDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void OminidirectionalShadowMapDemo::beforeDeviceCreation() {
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
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }

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

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void OminidirectionalShadowMapDemo::createDescriptorPool() {
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


void OminidirectionalShadowMapDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void OminidirectionalShadowMapDemo::createDescriptorSetLayouts() {
}

void OminidirectionalShadowMapDemo::updateDescriptorSets(){
}

void OminidirectionalShadowMapDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OminidirectionalShadowMapDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OminidirectionalShadowMapDemo::createRenderPipeline() {
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
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::vec3))
                .addDescriptorSetLayout(loader->descriptorSetLayout())
                .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
            .name("render")
        .build(render.layout);

    light.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("flat.vert.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .name("render_light")
        .build(light.layout);
    //    @formatter:on
}


void OminidirectionalShadowMapDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void OminidirectionalShadowMapDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *OminidirectionalShadowMapDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    // capture shadow
    shadowMap.capture([this, commandBuffer](auto& layout) {
        model->render(commandBuffer, layout, 1);
    }, commandBuffer, currentFrame);
    shadowMap.addBarrier(commandBuffer);


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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
//    static Camera aCamera{};
//    aCamera.view =  shadowMap._uniforms.cpu[1];
//    aCamera.proj = shadowMap._uniforms.cpu[0];
    camera->push(commandBuffer, render.layout);
//    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &aCamera);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(Camera), sizeof(glm::vec3), &lightPosition);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 2, 1, &bindlessDescriptor.descriptorSet, 0, VK_NULL_HANDLE);
    model->render(commandBuffer, render.layout);

    renderLight(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OminidirectionalShadowMapDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();

    static float elapsedTime = 0;
    elapsedTime += time;

    if(elapsedTime > 0 && int(elapsedTime) % 5 == 0) {
        ++view;
        view = (view % 7) + (view/7) * 1;
        spdlog::info("view: {}", view);
    }

    lightPosition.x = sin(glm::radians(elapsedTime * 360.0f)) * 0.15f;
    lightPosition.z = cos(glm::radians(elapsedTime * 360.0f)) * 0.15f;
    light.model = glm::translate(glm::mat4{1}, lightPosition);
    shadowMap.update(lightPosition);
}

void OminidirectionalShadowMapDemo::checkAppInputs() {
    camera->processInput();
}

void OminidirectionalShadowMapDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void OminidirectionalShadowMapDemo::onPause() {
    VulkanBaseApp::onPause();
}

void OminidirectionalShadowMapDemo::loadModel() {
//    model = loader->loadGltf(resource("shadowscene_fire.gltf"));
    model = loader->loadGltf(resource("Sponza/glTF/Sponza.gltf"));
    model->sync();
}

void OminidirectionalShadowMapDemo::initShadowMap() {
    shadowMap = PointShadowMap{ device, descriptorPool, MAX_IN_FLIGHT_FRAMES, VK_FORMAT_D16_UNORM};
    shadowMap.init();
    shadowMap.setRange(50);
    bindlessDescriptor.update({ &shadowMap.shadowMap(0), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0 });
}

void OminidirectionalShadowMapDemo::endFrame() {
    auto nextFrame = (currentFrame + 1) % MAX_IN_FLIGHT_FRAMES;
    bindlessDescriptor.update({ &shadowMap.shadowMap(nextFrame), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0 });
}

void OminidirectionalShadowMapDemo::createLights() {
    auto sphere = primitives::sphere(10, 10, 0.1, glm::mat4{1}, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

    lightObj.vertices = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    lightObj.indexes = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void OminidirectionalShadowMapDemo::renderLight(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    camera->push(commandBuffer, light.layout, light.model);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, light.pipeline.handle);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, lightObj.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, lightObj.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, lightObj.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
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
        settings.deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = OminidirectionalShadowMapDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}