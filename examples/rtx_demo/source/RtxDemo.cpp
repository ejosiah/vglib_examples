#include "RtxDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

RtxDemo::RtxDemo(const Settings& settings) : VulkanBaseApp("RTX Demo", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("rtx_demo");
    fileManager().addSearchPathFront("rtx_demo/data");
    fileManager().addSearchPathFront("rtx_demo/spv");
    fileManager().addSearchPathFront("rtx_demo/models");
    fileManager().addSearchPathFront("rtx_demo/textures");
}

void RtxDemo::initApp() {
    initCamera();
    initBindlessDescriptor();
    initBuffers();
    initUniforms();
    initRenderInfo();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    loadScene();

    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void RtxDemo::initBuffers() {
    textures::create(device, colorBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, normalBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {width, height, 1});
    textures::create(device, depthBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    colorBufferIndex = bindlessDescriptor.update(colorBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    depthBufferIndex = bindlessDescriptor.update(depthBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void RtxDemo::initUniforms() {
    UniformData defaults{
        .viewportSize =  { width, height },
        .near =  1.0,
        .far =  10.0f
    };

    uniforms.gpu = device.createCpuVisibleBuffer(&defaults, sizeof(defaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData *>(uniforms.gpu.map());
}

void RtxDemo::initRenderInfo() {
    renderInfo = {
        .colorAttachments = {
            { colorBuffer.imageView, VK_FORMAT_R32G32B32A32_SFLOAT },
        },
        .depthAttachment = {{ .imageView = depthBuffer.imageView, .format = VK_FORMAT_D16_UNORM, .clear = false }},
        .renderArea = { width, height}
    };
    dppRenderInfo = {
        .depthAttachment = {{ .imageView = depthBuffer.imageView, .format = VK_FORMAT_D16_UNORM }},
        .renderArea = { width, height}
    };
}

void RtxDemo::initCamera() {
    // OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
    // cameraSettings.orbitMinZoom = 0.1;
    // cameraSettings.orbitMaxZoom = 512.0f;
    // cameraSettings.offsetDistance = 1.0f;
    // cameraSettings.sceneHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void RtxDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void RtxDemo::beforeDeviceCreation() {
    enabledFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
    enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;

    auto features12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    features12->scalarBlockLayout = VK_TRUE;
    features12->descriptorIndexing = VK_TRUE;
    features12->runtimeDescriptorArray = VK_TRUE;
    features12->bufferDeviceAddress = VK_TRUE;
    features12->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->maintenance4 = VK_TRUE;
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;
    dsFeatures->extendedDynamicState3ColorBlendEnable = VK_TRUE;

    auto indexType8 = findExtension<VkPhysicalDeviceIndexTypeUint8FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT, deviceCreateNextChain);
    indexType8->indexTypeUint8 = VK_TRUE;

    // Add raytracing device extensions
    deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);


    auto enabledRayTracingPipelineFeatures = findExtension<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, deviceCreateNextChain);
    enabledRayTracingPipelineFeatures->rayTracingPipeline = VK_TRUE;

    auto enabledAccelerationStructureFeatures = findExtension<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, deviceCreateNextChain);
    enabledAccelerationStructureFeatures->accelerationStructure = VK_TRUE;

    auto rayQueryFeature = findExtension<VkPhysicalDeviceRayQueryFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR, deviceCreateNextChain);
    rayQueryFeature->rayQuery = VK_TRUE;

    auto fetchFeature = findExtension<VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR, deviceCreateNextChain);
    fetchFeature->rayTracingPositionFetch = VK_TRUE;
}

void RtxDemo::createDescriptorPool() {
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


void RtxDemo::initLoader() {
    gltf::bvh::Bvh::createDescriptorSetLayout(device);
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}


void RtxDemo::loadScene() {
    scene = loader->loadGltf(resource("Sponza/glTF/Sponza.gltf"));
    scene->sync();

    lights = scene->lights.span<gltf::Light>();
    lights[0].direction = glm::vec3(0, -1, 0);
    lights[0].intensity = 100;
    lights[0].range = 10;
    lights[0].color = glm::vec3(1.0);
    lights[0].type = to<int>(gltf::LightType::POINT);

    lightInstances = scene->lightInstances.span<gltf::LightInstance>();
    lightInstances[0].model = glm::translate(glm::mat4(1), glm::vec3(0, 3, 0));
    lightInstances[0].ModelInverse = glm::inverse(lightInstances[0].model);

    bvh = gltf::bvh::Bvh{ device, descriptorPool, scene };
    bvh.build();
}

void RtxDemo::createDescriptorSetLayouts() {
    uniformDescriptorSetLayout =
     device.descriptorSetLayoutBuilder()
         .name("uniforms_set_layout")
         .binding(0)
             .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
             .descriptorCount(1)
             .shaderStages(VK_SHADER_STAGE_ALL)
     .createLayout();

}

void RtxDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ uniformDescriptorSetLayout });
    uniformDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = uniformDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{uniforms.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uniformInfo;

    device.updateDescriptorSets(writes);
}

void RtxDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void RtxDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void RtxDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pbr.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
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
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(gltf::bvh::Bvh::rtxDescriptorSetLayout)
                .name("pbr_render")
                .build(render.pbr.layout);

        render.prePass.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(VertexMultiAttributes::bindingDescription())
                    .addVertexAttributeDescriptions(VertexMultiAttributes::attributeDescription())
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
                .dynamicRenderPass()
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(gltf::bvh::Bvh::rtxDescriptorSetLayout)
                .name("depth_pre_pass")
            .build(render.prePass.layout);

        render.fullscreen.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("fullscreen.vert.spv"))
                    .fragmentShader(resource("fullscreen.frag.spv"))
                .vertexInputState().clear()
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("fullscreen_render")
                .build(render.fullscreen.layout);

        render.depthBufferVis.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("fullscreen.vert.spv"))
                    .fragmentShader(resource("view_normal.frag.spv"))
                .vertexInputState().clear()
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addDescriptorSetLayout(uniformDescriptorSetLayout)
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("depth_buffer_render")
                .build(render.depthBufferVis.layout);
    //    @formatter:on
}


void RtxDemo::onSwapChainDispose() {
    dispose(render.pbr.pipeline);
}

void RtxDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    uniforms.cpu->viewportSize = { width, height};

    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *RtxDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    depthPrepass(commandBuffer);

    clearColor(0, 0, 0);

    renderToSwapChain([&]{
        // renderScene(commandBuffer, render.pbr);
        visualizeDepthBuffer(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void RtxDemo::depthPrepass(VkCommandBuffer commandBuffer) {
    Offscreen::render(commandBuffer, dppRenderInfo, [&] {
       renderScene(commandBuffer, render.prePass);
    });

}

void RtxDemo::visualizeDepthBuffer(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.depthBufferVis.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.depthBufferVis.layout.handle, 0, 1, &uniformDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.depthBufferVis.layout.handle, 1, 1, &bindlessDescriptor.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 3, 1, 0, depthBufferIndex);}


void RtxDemo::renderScene(VkCommandBuffer commandBuffer, const Pipeline& pipeline) {
    camera->push(commandBuffer, pipeline.layout);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 3, 1, &scene->rtxDescriptorSet, 0, VK_NULL_HANDLE);
    scene->renderWithMaterial(commandBuffer, pipeline.layout);
}

void RtxDemo::renderFullscreenQuad(VkCommandBuffer commandBuffer, uint textureIndex) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.fullscreen.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.fullscreen.layout.handle, 0, 1, &bindlessDescriptor.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 3, 1, 0, textureIndex);
}

void RtxDemo::update(float time) {
    camera->update(time);
    setTitle(fmt::format("{}, FPS: {}", title, framePerSecond));
}

void RtxDemo::checkAppInputs() {
    camera->processInput();
}

void RtxDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void RtxDemo::onPause() {
    VulkanBaseApp::onPause();
}

void RtxDemo::newFrame() {
    camera->newFrame();
}

void RtxDemo::endFrame() {
    uniforms.cpu->projection = camera->cam().proj;
    uniforms.cpu->view = camera->cam().view;
    uniforms.cpu->model = camera->cam().model;
    uniforms.cpu->inverseProjection = glm::inverse(camera->cam().proj);
    uniforms.cpu->inverseView = glm::inverse(camera->cam().view);
    uniforms.cpu->previousViewProjection = camera->previousCamera().proj * camera->previousCamera().view;
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
        settings.depthTest = true;
        // settings.fullscreen = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT | VK_QUEUE_COMPUTE_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = RtxDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}