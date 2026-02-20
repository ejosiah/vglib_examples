#include "RtxDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "spectrum/spectrum.hpp"

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
    initBindlessDescriptor();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    initLights();
    createDescriptorSetLayouts();
    initCamera();
    initBuffers();
    loadScene();
    initUniforms();
    initRenderInfo();
    updateDescriptorSets();
    initShadow();
    initDDGI();

    createCommandPool();
    createPipelineCache();
    createRenderPipeline();

    jitter.sampler.type = static_cast<SamplerType>(SamplerType::Interleaved_Gradients);
    jitter.period(4);
}

void RtxDemo::initBuffers() {
    textures::create(device, colorBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, normalBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {width, height, 1});
    textures::create(device, depthBuffer, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    colorBufferIndex = bindlessDescriptor.update(colorBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    normalBufferIndex = bindlessDescriptor.update(normalBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    depthBufferIndex = bindlessDescriptor.update(depthBuffer, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    auto prim = primitives::sphere(10, 10, 1.0, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    sphere.vertices = device.createDeviceLocalBuffer(prim.vertices.data(), BYTE_SIZE(prim.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    sphere.indexes = device.createDeviceLocalBuffer(prim.indices.data(), BYTE_SIZE(prim.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

}

void RtxDemo::initUniforms() {
    UniformData defaults{};

    uniforms.gpu = device.createCpuVisibleBuffer(&defaults, sizeof(defaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData *>(uniforms.gpu.map());
}

void RtxDemo::initRenderInfo() {
    renderInfo = {
        .colorAttachments = {
            { colorBuffer.imageView, VK_FORMAT_R32G32B32A32_SFLOAT },
            { normalBuffer.imageView, VK_FORMAT_R32G32_SFLOAT },
        },
        .depthAttachment = {{ .imageView = depthBuffer.imageView, .format = VK_FORMAT_D16_UNORM }},
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

    const auto extent = swapChain.extent;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(extent.width)/float(extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    cameraInfo = std::make_shared<CameraInfo>(device, descriptorPool, camera->camera, extent.width, extent.height, camera->near(), camera->far() );
    cameraInfo->init();
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

    lightInfo.descriptorSetLayout =
     device.descriptorSetLayoutBuilder()
         .name("light_set_layout")
         .binding(0)
             .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
             .descriptorCount(1)
             .shaderStages(VK_SHADER_STAGE_ALL)
         .binding(1)
             .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
             .descriptorCount(1)
             .shaderStages(VK_SHADER_STAGE_ALL)
     .createLayout();

}

void RtxDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ uniformDescriptorSetLayout, lightInfo.descriptorSetLayout });
    uniformDescriptorSet = sets[0];
    lightInfo.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = uniformDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{uniforms.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uniformInfo;

    writes[1].dstSet = lightInfo.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo lights{lightInfo.lightBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &lights;

    writes[2].dstSet = lightInfo.descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo lightInstance{lightInfo.lightInstanceBuffer, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &lightInstance;

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
                    .attachment()
                    .add()
                .dynamicState()
                    .colorBlendEnable()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(loader->descriptorSetLayout())
                    .addDescriptorSetLayout(loader->materialDescriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(gltf::bvh::Bvh::rtxDescriptorSetLayout)
                    .addDescriptorSetLayout(lightInfo.descriptorSetLayout)
                    .addDescriptorSetLayout(*cameraInfo->descriptorSetLayout())
                    .addDescriptorSetLayout(uniformDescriptorSetLayout)
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

        render.toneMap.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("fullscreen.vert.spv"))
                    .fragmentShader(resource("tone_map.frag.spv"))
                .vertexInputState().clear()
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("tone_mapper")
                .build(render.toneMap.layout);

        render.depthBufferVis.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("fullscreen.vert.spv"))
                    .fragmentShader(resource("view_normal.frag.spv"))
                .vertexInputState().clear()
                .rasterizationState()
                    .cullNone()
                .layout().clear()
                    .addDescriptorSetLayout(*cameraInfo->descriptorSetLayout())
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("depth_buffer_render")
                .build(render.depthBufferVis.layout);

        render.lights.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("lights.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .depthStencilState()
                    .disableDepthWrite()
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
                    .attachment()
                    .add()
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout()
                    .addDescriptorSetLayout(lightInfo.descriptorSetLayout)
                .name("light_render")
            .build(render.lights.layout);

        render.probe.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("probes.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .depthStencilState()
                    .disableDepthWrite()
                .colorBlendState()
                    .attachments(2)
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(probeConstants))
                .name("probe_render")
            .build(render.probe.layout);


    //    @formatter:on
}


void RtxDemo::onSwapChainDispose() {
    dispose(render.pbr.pipeline);
}

void RtxDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    cameraInfo->cpu().viewportSize = { width, height};

    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *RtxDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    device.group([&]{
        device.group([&]{
            Offscreen::render(commandBuffer, renderInfo, [&]{
                renderScene(commandBuffer, render.pbr);
                renderLights(commandBuffer);
//                renderProbes(commandBuffer);
            });
            Barrier::fragmentWriteToComputeRead(commandBuffer);
        }, commandBuffer, "lighting_pass");

        shadow.exec(commandBuffer);
        ddgi.exec(commandBuffer);

        clearColor(0, 0, 0);
        renderToSwapChain([&]{
    //        visualizeDepthBuffer(commandBuffer);
            toneMap(commandBuffer);
//            renderFullscreenQuad(commandBuffer, ddgi.indirectLight());
        }, commandBuffer);
    }, commandBuffer, "frame");



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
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.depthBufferVis.layout.handle, 0, 1, cameraInfo->descriptorSet(), 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.depthBufferVis.layout.handle, 1, 1, &bindlessDescriptor.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 3, 1, 0, depthBufferIndex);}


void RtxDemo::renderScene(VkCommandBuffer commandBuffer, const Pipeline& pipeline) {
    static VkBool32 blendingEnabled = VK_FALSE;

    camera->push(commandBuffer, pipeline.layout);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 3, 1, &scene->rtxDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 4, 1, &lightInfo.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 5, 1, cameraInfo->descriptorSet(), 0, VK_NULL_HANDLE);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 6, 1, &uniformDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdSetColorBlendEnableEXT(commandBuffer, 1, 1, &blendingEnabled);
    scene->renderWithMaterial(commandBuffer, pipeline.layout);
}

void RtxDemo::toneMap(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMap.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.toneMap.layout.handle, 0, 1, &bindlessDescriptor.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 3, 1, 0, colorBufferIndex);
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
    jitterValue = (-1.f + 2.f * jitter.nextSample()) * .5f;
    jitterValue /= glm::vec2(width, height);

    cameraInfo->newFrame();
    camera->newFrame();
//    camera->jitter(jitterValue.x, jitterValue.y);
    shadow.newFrame();
    ddgi.newFrame();
}

void RtxDemo::endFrame() {
    shadow.endFrame();
    cameraInfo->endFrame();
    ddgi.endFrame();

    auto offset = ddgi.probes().count/2 - ddgi.probes().count;

    probeConstants.model = glm::translate(glm::mat4{1}, {offset.x, 0.5, offset.z}) * glm::scale(glm::mat4{1}, glm::vec3(0.05));
    probeConstants.viewProjection =  camera->cam().proj * camera->cam().view;
    probeConstants.probeCount = ddgi.probes().count;
    probeConstants.probeSpacing = ddgi.probes().spacing;
}

void RtxDemo::initShadow() {
    shadow = rtx::shadow{{ device, bindlessDescriptor, descriptorPool, cameraInfo, lightInfo.descriptorSetLayout,
                           gltf::bvh::Bvh::rtxDescriptorSetLayout, lightInfo.descriptorSet, scene->rtxDescriptorSet,
                          1, depthBufferIndex, normalBufferIndex }};
    shadow.init();

    for(auto& light : lightInfo.lights) {
        light.shadowMapIndex = static_cast<int>(shadow.visibility());
    }
}

void RtxDemo::initDDGI() {
    glm::vec3 sceneHalfWidth = (scene->bounds.max - scene->bounds.min) * 0.5f;
    ddgi = rtx::ddgi{{ device, bindlessDescriptor, descriptorPool, cameraInfo, lightInfo.descriptorSetLayout,
                               gltf::bvh::Bvh::rtxDescriptorSetLayout, lightInfo.descriptorSet, scene->rtxDescriptorSet,
                              1, depthBufferIndex, normalBufferIndex, sceneHalfWidth }};
    ddgi.init();
    uniforms.cpu->indirect_light_texture_index = ddgi.indirectLight();
    numProbes = ddgi.probes().count.x * ddgi.probes().count.y * ddgi.probes().count.z;
}

void RtxDemo::initLights() {
    const auto numLights = lightInfo.numLights;
    lightInfo.lightBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(gltf::Light) * numLights, "lights");
    lightInfo.lightInstanceBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(gltf::LightInstance) * numLights, "light_instances");
    lightInfo.lights = lightInfo.lightBuffer.span<gltf::Light>(numLights);
    lightInfo.lightInstances = lightInfo.lightInstanceBuffer.span<gltf::LightInstance>(numLights);

    const auto intensity = 10.0f;
    lightInfo.lights[0].direction = glm::vec3(0, -1, 0);
    lightInfo.lights[0].intensity = intensity;
    lightInfo.lights[0].range = 5;
    lightInfo.lights[0].color = spectrum::blackbodySpectrum({5000, intensity}).front();
    lightInfo.lights[0].type = to<int>(gltf::LightType::POINT);

    glm::mat4 model{1};
    model = glm::translate(model, {0, 1, -2});
    lightInfo.lightInstances[0].model = model;
    lightInfo.lightInstances[0].ModelInverse = glm::inverse(model);

}

void RtxDemo::renderLights(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lights.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lights.layout.handle, 0, 1, &lightInfo.descriptorSet, 0, 0);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, sphere.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, sphere.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, sphere.indexes.sizeAs<uint32_t>(), lightInfo.numLights, 0, 0, 0);
}


void RtxDemo::renderProbes(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.probe.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.probe.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(probeConstants), &probeConstants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, sphere.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, sphere.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, sphere.indexes.sizeAs<uint32_t>(), numProbes, 0, 0, 0);
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
        settings.enabledFeatures.independentBlend = true;
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