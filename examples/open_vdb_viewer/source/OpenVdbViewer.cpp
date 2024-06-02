#include "OpenVdbViewer.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "L2DFileDialog.h"
#include <openvdb/openvdb.h>
#include "glm_format.h"
#include "primitives.h"
#include "vulkan_image_ops.h"

#include <queue>
#include <sstream>
#include <future>

OpenVdbViewer::OpenVdbViewer(const Settings& settings) : VulkanBaseApp("Open Vdb viewer", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/open_vdb_viewer");
    fileManager().addSearchPathFront("../../examples/open_vdb_viewer/data");
    fileManager().addSearchPathFront("../../examples/open_vdb_viewer/spv");
    fileManager().addSearchPathFront("../../examples/open_vdb_viewer/models");
    fileManager().addSearchPathFront("../../examples/open_vdb_viewer/textures");

    static VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT feature{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
    feature.mutableDescriptorType = VK_TRUE;
    syncFeatures = VkPhysicalDeviceSynchronization2Features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            &feature,
            VK_TRUE
    };
    deviceCreateNextChain = &syncFeatures;
}

void OpenVdbViewer::initApp() {
    openvdb::initialize();
    createRenderTarget();
    initCamera();
    initAnimation();
    createBuffers();
    createPlaceHolderTexture();
    createDescriptorPool();
    createSamplers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    updateVolumeDescriptorSets();
    updateSceneDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    blur = std::make_unique<Blur>(&device, &descriptorPool, &fileManager(), width, height);

}

void OpenVdbViewer::createRenderTarget() {
    auto usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageCreateInfo info = initializers::imageCreateInfo(VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, usage, width, height);

    // create color buffer
    VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    GBuffer.color.image = device.createImage(info);
    GBuffer.color.imageView = GBuffer.color.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
    textures::create(device, previousFrameTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));

    // create depth buffer
    info.format = VK_FORMAT_D32_SFLOAT;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    GBuffer.depth.image = device.createImage(info);
    GBuffer.depth.imageView = GBuffer.depth.image.createView(info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        GBuffer.depth.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                             subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        GBuffer.color.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                      subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        previousFrameTexture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                    subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });

    VkAttachmentDescription attachment{
            0,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_SAMPLE_COUNT_1_BIT,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_ATTACHMENT_STORE_OP_STORE,
            VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            VK_ATTACHMENT_STORE_OP_DONT_CARE,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL
    };

    std::vector<VkAttachmentDescription> attachmentDesc;
    attachmentDesc.push_back(attachment);


    attachment.format = VK_FORMAT_D32_SFLOAT;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;
    attachmentDesc.push_back(attachment);

    SubpassDescription subpass{};
    subpass.colorAttachments = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}
    };
    subpass.depthStencilAttachments.attachment = 1;
    subpass.depthStencilAttachments.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL;

    std::vector<SubpassDescription> subpassDesc{ subpass };

    std::vector<VkSubpassDependency> dependencies{
            {
                    VK_SUBPASS_EXTERNAL,
                    0,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    0
            },
            {
                    0,
                    VK_SUBPASS_EXTERNAL,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    0
            }
    };


    renderTarget.renderPass = device.createRenderPass(attachmentDesc, subpassDesc, dependencies);

    std::vector<VkImageView> attachments{
            GBuffer.color.imageView.handle,
            GBuffer.depth.imageView.handle,
    };
    renderTarget.framebuffer = device.createFramebuffer(renderTarget.renderPass, attachments, width, height);
}

void OpenVdbViewer::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 1000.0f;
    cameraSettings.offsetDistance = 2.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void OpenVdbViewer::initAnimation() {
    animation = std::make_unique<VdbAnimation>( &device, 24);
    animation->onFramesReady([this]{ onFrameReady(); });
    animation->onFrameLoadFailed([this]{ onFrameLoadFailure(); });
    animation->init();
}

void OpenVdbViewer::onFrameReady() {
    onIdle([this]{
        vkDeviceWaitIdle(device);
        spdlog::info("exporting volume to GPU");
        animation->updateDescriptorSet();
        loadState = LoadState::READY;
        vdbPath.clear();
    });
}

void OpenVdbViewer::onFrameLoadFailure() {
    onIdle([this]{ loadState = LoadState::FAILED; });
}

void OpenVdbViewer::updateCamera() {
    auto diagonal = volumeUbo->boxMax - volumeUbo->boxMin;
    auto halfMaxDist = glm::max(diagonal.x, glm::max(diagonal.y, diagonal.z)) * 0.5f;
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 1000.0f;
    cameraSettings.offsetDistance = halfMaxDist + halfMaxDist * .25f;
    cameraSettings.modelHeight = diagonal.y * 0.5f;
    cameraSettings.fieldOfView = 90.0f;
    cameraSettings.horizontalFov = true;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->zoomDelta = 1;
}

void OpenVdbViewer::createBuffers() {
    cameraUboBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(CameraUbo));
    volumeUboBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(VolumeUbo));
    sceneBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneUbo));

    cameraUbo = reinterpret_cast<CameraUbo*>(cameraUboBuffer.map());
    volumeUbo = reinterpret_cast<VolumeUbo*>(volumeUboBuffer.map());
    volumeUbo->time = 0;
    volumeUbo->boxMin = glm::vec3(-.5);
    volumeUbo->boxMax = glm::vec3(.5);
    volumeUbo->color = glm::vec3(1);
    volumeUbo->numSamples = 100;
    volumeUbo->coneSpread = 10;
    volumeUbo->lightIntensity = 200;
    volumeUbo->g = 0.2;
    volumeUbo->frame = 0;
    volumeUbo->width = width;
    volumeUbo->height = height;
    volumeUbo->invMaxDensity = 0;
    volumeUbo->lightPosition = glm::vec3(0);
    volumeUbo->scatteringCoefficient = 60;
    volumeUbo->absorptionCoefficient = 40;
    volumeUbo->extinctionCoefficient = volumeUbo->scatteringCoefficient + volumeUbo->absorptionCoefficient;
    updateCamera();

    sceneUbo = reinterpret_cast<SceneUbo*>(sceneBuffer.map());
    *sceneUbo = SceneUbo{width, height, 0, 0, 0, MAX_SAMPLES, 0, 5};

    auto vertices = ClipSpace::Quad::positions;
    vertexBuffer = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    placeHolderVertexBuffer = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec4));

    auto sphere = primitives::sphere(50, 50, 1.0f, glm::mat4{1}, glm::vec4(1, 0, 0, 0));
    light.vertexBuffer = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    light.indexBuffer = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    light.intensity = 200;

}

void OpenVdbViewer::createPlaceHolderTexture() {
    std::vector<float> placeHolder(8);

    textures::create(device, volumeTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, placeHolder.data(),
                     {2, 2, 2}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));
}

void OpenVdbViewer::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 11> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets },
            }
    };

    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

    poolSizes[0].descriptorCount = 1000;
    cpuDescriptorPool = device.createDescriptorPool(1000, poolSizes
                                                    , VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_HOST_ONLY_BIT_EXT );

}

void OpenVdbViewer::createSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    linearSampler = device.createSampler(samplerInfo);
}

void OpenVdbViewer::createDescriptorSetLayouts() {
    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
    volumeDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    sceneDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    renderDescriptorSetSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
                .immutableSamplers(linearSampler)
            .createLayout();
    
    auto sets = descriptorPool.allocate({ descriptorSetLayout, volumeDescriptorSetLayout, renderDescriptorSetSetLayout, sceneDescriptorSetLayout});
    
    
    descriptorSet = sets[0];
    volumeDescriptor = sets[1];
    renderDescriptorSet = sets[2];
    sceneDescriptorSet = sets[3];

}

void OpenVdbViewer::updateDescriptorSets(){
    
    auto writes = initializers::writeDescriptorSets();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo cameraInfo{cameraUboBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &cameraInfo;
    
    device.updateDescriptorSets(writes);
}

void OpenVdbViewer::updateVolumeDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<3>();
    
    writes[0].dstSet = volumeDescriptor;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{ volumeUboBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &bufferInfo;
    
    writes[1].dstSet = volumeDescriptor;
    writes[1].dstBinding = volumeBinding;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ volumeTexture.sampler.handle, volumeTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1].pImageInfo = &imageInfo;
    
    writes[2].dstSet = renderDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo renderImageInfo{ VK_NULL_HANDLE, GBuffer.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &renderImageInfo;
    
    device.updateDescriptorSets(writes);
}

void OpenVdbViewer::updateSceneDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = sceneDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{ sceneBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &bufferInfo;

    writes[1].dstSet = sceneDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ linearSampler.handle, previousFrameTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &imageInfo;

    device.updateDescriptorSets(writes);
}

void OpenVdbViewer::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OpenVdbViewer::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OpenVdbViewer::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    rayMarching.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(resource("volume.vert.spv"))
                .fragmentShader(resource("ray_marcher.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(swapChain.extent)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(swapChain.extent)
                .add()
                .rasterizationState()
                    .cullBackFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(settings.msaaSamples)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addDescriptorSetLayout(descriptorSetLayout)
                    .addDescriptorSetLayout(volumeDescriptorSetLayout)
                .renderPass(renderTarget.renderPass)
                .subpass(0)
                .name("ray_marching")
                .pipelineCache(pipelineCache)
            .build(rayMarching.layout);

    deltaTracking.pipeline =
        builder
            .basePipeline(rayMarching.pipeline)
            .shaderStage()
                .fragmentShader(resource("delta_tracking.frag.spv"))
            .name("delta_tracking")
        .build(deltaTracking.layout);

    pathTracer.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("path_tracing.frag.spv"))
            .layout()
                .addDescriptorSetLayout(sceneDescriptorSetLayout)
            .name("path_tracing")
        .build(pathTracer.layout);

    background.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("background.frag.spv"))
            .depthStencilState()
                .disableDepthTest()
                .disableDepthWrite()
            .name("background")
        .build(background.layout);
			
//    sliceRenderer.pipeline =
//        builder
//            .allowDerivatives()
//            .shaderStage()
//                .vertexShader(resource("slice.vert.spv"))
//                .geometryShader(resource("slice.geom.spv"))
//                .fragmentShader(resource("slice.frag.spv"))
//            .vertexInputState().clear()
//                .addVertexBindingDescription(0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX)
//                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0)
//            .inputAssemblyState()
//                .points()
//                .colorBlendState()
//                    .attachment().clear()
//                        .enableBlend()
//                        .colorBlendOp().add()
//                        .alphaBlendOp().add()
//                        .srcColorBlendFactor().srcAlpha()
//                        .dstColorBlendFactor().oneMinusSrcAlpha()
//                        .srcAlphaBlendFactor().one()
//                        .dstAlphaBlendFactor().zero()
//                    .add()
//                .layout().clear()
//                    .addDescriptorSetLayout(volumeDescriptorSetLayout)
//                    .addPushConstantRange(VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(sliceRenderer.constants))
//                .renderPass(renderPass)
//                .subpass(0)
//                .name("volume_slice_renderer")
//                .pipelineCache(pipelineCache)
//            .build(sliceRenderer.layout);
    //    @formatter:on

    light.pipeline =
        builder
            .shaderStage().clear()
                .vertexShader(resource("flat.vert.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
            .inputAssemblyState()
                .triangleStrip()
                .enablePrimitiveRestart()
            .rasterizationState()
                .cullNone()
            .depthStencilState()
                .enableDepthWrite()
                .enableDepthTest()
                .compareOpLess()
                .minDepthBounds(0)
                .maxDepthBounds(1)
            .colorBlendState()
                .attachments(1)
            .layout().clear()
                .addPushConstantRange(Camera::pushConstant())
            .name("light_renderer")
        .build(light.layout);

    screenQuad.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("screen.vert.spv"))
                .fragmentShader(resource("screen.frag.spv"))
            .vertexInputState().clear()
            .inputAssemblyState()
                .triangles()
//            .depthStencilState()
//                .disableDepthTest()
//                .disableDepthWrite()
            .colorBlendState()
                .attachments(1)
            .layout().clear()
                .addDescriptorSetLayout(renderDescriptorSetSetLayout)
            .renderPass(renderPass)
            .name("screen_quad")
        .build(screenQuad.layout);
}


void OpenVdbViewer::onSwapChainDispose() {
    dispose(rayMarching.pipeline);
}

void OpenVdbViewer::onSwapChainRecreation() {
    createRenderTarget();

    sceneUbo->width = width;
    sceneUbo->height = height;
    sceneUbo->currentSample = 0;

    updateDescriptorSets();
    updateVolumeDescriptorSets();
    updateSceneDescriptorSets();
    createRenderPipeline();
    blur->refresh(width, height);

}

void OpenVdbViewer::offscreenRender() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        if(renderer == Renderer::PATH_TRACING){
            static std::array<VkClearValue, 2> clearValues;
            clearValues[0].color = {0, 0, 0, 1};
            clearValues[1].depthStencil = {1.0, 0u};

            VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
            rPassInfo.clearValueCount = COUNT(clearValues);
            rPassInfo.pClearValues = clearValues.data();
            rPassInfo.framebuffer = renderTarget.framebuffer;
            rPassInfo.renderArea.offset = {0u, 0u};
            rPassInfo.renderArea.extent = swapChain.extent;
            rPassInfo.renderPass = renderTarget.renderPass;

            vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            renderWithPathTracing(commandBuffer);
            vkCmdEndRenderPass(commandBuffer);

            device.imageOps()
                .srcImage(GBuffer.color.image)
                    .aspectMask(VK_ACCESS_SHADER_WRITE_BIT)
                    .pipelineStage(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                .dstImage(previousFrameTexture.image)
                    .aspectMask(VK_ACCESS_SHADER_READ_BIT)
                    .pipelineStage(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
                .imageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1)
                .extent(width, height, 1)
            .copy(commandBuffer);

        }else{
            static std::array<VkClearValue, 2> clearValues;
            clearValues[0].color = {0, 0, 0, 1};
            clearValues[1].depthStencil = {1.0, 0u};

            VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
            rPassInfo.clearValueCount = COUNT(clearValues);
            rPassInfo.pClearValues = clearValues.data();
            rPassInfo.framebuffer = renderTarget.framebuffer;
            rPassInfo.renderArea.offset = {0u, 0u};
            rPassInfo.renderArea.extent = swapChain.extent;
            rPassInfo.renderPass = renderTarget.renderPass;

            vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            renderBackground(commandBuffer);
            renderLight(commandBuffer);
            renderVolume(commandBuffer);

            vkCmdEndRenderPass(commandBuffer);

            if(doBlur){
                blur->execute(commandBuffer, GBuffer.color.image, GBuffer.color.image, blurIterations);
            }
        }
    });
}

VkCommandBuffer *OpenVdbViewer::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    renderFullscreenQuad(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OpenVdbViewer::renderFullscreenQuad(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = renderDescriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screenQuad.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screenQuad.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void OpenVdbViewer::renderLight(VkCommandBuffer commandBuffer) {
    static glm::mat4 xform;
    xform = glm::translate(glm::mat4{1}, light.position);
    xform = glm::scale(xform , glm::vec3(light.scale));
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, light.pipeline.handle);
    camera->push(commandBuffer, light.layout, xform);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, light.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, light.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, light.indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void OpenVdbViewer::renderVolume(VkCommandBuffer commandBuffer) {
    switch(renderer){
        case Renderer::DELTA_TRACKING:
            renderWithDeltaTracking(commandBuffer);
            break;
        case Renderer::RAY_MARCHING:
            renderWithRayMarching(commandBuffer);
            break;
        default:
            throw std::runtime_error{"invalid renderer"};
    }
}

void OpenVdbViewer::renderWithRayMarching(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = descriptorSet;
    sets[1] = volumeDescriptor;
    VkDeviceSize  offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarching.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarching.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void OpenVdbViewer::renderWithDeltaTracking(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = descriptorSet;
    sets[1] = volumeDescriptor;
    VkDeviceSize  offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, deltaTracking.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, deltaTracking.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void OpenVdbViewer::renderWithPathTracing(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = descriptorSet;
    sets[1] = volumeDescriptor;
    sets[2] = sceneDescriptorSet;
    VkDeviceSize  offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pathTracer.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pathTracer.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void OpenVdbViewer::renderBackground(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = descriptorSet;
    sets[1] = volumeDescriptor;
    VkDeviceSize  offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void OpenVdbViewer::renderVolumeSlices(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sliceRenderer.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sliceRenderer.layout.handle, 0, 1, &volumeDescriptor,
                            0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, sliceRenderer.layout.handle, VK_SHADER_STAGE_GEOMETRY_BIT, 0,
                       sizeof(sliceRenderer.constants), &sliceRenderer.constants);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 1, sliceRenderer.constants.numSlices, 0, 0);
}

void OpenVdbViewer::renderUI(VkCommandBuffer commandBuffer) {

    if(ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            auto enabled = loadState == LoadState::READY;
            if(ImGui::MenuItem("Open File...", nullptr, false, enabled)) {
                FileDialog::file_dialog_open = true;
                FileDialog::file_dialog_open_type = FileDialog::FileDialogType::OpenFile;
            }

            if(ImGui::MenuItem("Open Folder...", nullptr, false, enabled)) {
                FileDialog::file_dialog_open = true;
                FileDialog::file_dialog_open_type = FileDialog::FileDialogType::SelectFolder;
            }

            if(ImGui::MenuItem("Exit")){
                this->exit->press();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    openFileDialog();

    if(loadState == LoadState::FAILED){
        ImGui::Begin("Alert");
        ImGui::SetWindowSize({350, 150});
        ImGui::Text("failed opening file %s", fs::path{vdbPath}.filename().string().c_str());
        if(ImGui::Button("Close")){
            loadState = LoadState::READY;
            vdbPath.clear();
        }
        ImGui::End();
    }

    ImGui::Begin("Settings");
    ImGui::SetWindowSize({0, 0});

    static int rendererOption = static_cast<int>(renderer);
    ImGui::Text("Renderer:");
    ImGui::Indent(16);
    ImGui::RadioButton("ray marching", &rendererOption, 0); ImGui::SameLine();
    ImGui::RadioButton("delta tracking", &rendererOption, 1); ImGui::SameLine();
    ImGui::RadioButton("path tracer", &rendererOption, 2);
    renderer = static_cast<Renderer>(rendererOption);
    ImGui::Indent(-16);
    ImGui::Separator();

    ImGui::SliderInt("num samples", &volumeUbo->numSamples, 1, 1000);
    ImGui::SliderFloat("Cone spread", &volumeUbo->coneSpread, 0.1, 50);
    ImGui::SliderFloat("g", &volumeUbo->g, -0.999, 0.999);
    ImGui::SliderFloat("intensity", &volumeUbo->lightIntensity, 1, 1000);

    ImGui::Separator();

    if(renderer == Renderer::PATH_TRACING){
        ImGui::Text("Path tracing:");
        ImGui::Indent(16);
        ImGui::Text("%d of %d samples remaining", sceneUbo->currentSample, sceneUbo->numSamples);
        ImGui::Indent(-16);
    }else {
        ImGui::Text("Blur:");
        ImGui::Indent(16);
        ImGui::Checkbox("blur", &doBlur);
        if (doBlur) {
            ImGui::SliderInt("iterations", &blurIterations, 1, 9);
        }
        ImGui::Indent(-16);
    }

    ImGui::Separator();
    if(ImGui::Button("next frame")) {
    }

    ImGui::Separator();
    ImGui::ColorEdit3("color", &volumeUbo->color.x);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

bool OpenVdbViewer::openFileDialog() {
    static char* file_dialog_buffer = nullptr;
    static char path[500] = R"(C:\Users\Josiah Ebhomenye\OneDrive\media\volumes\_VDB-Smoke-Pack)";

    file_dialog_buffer = path;;

    static bool closed = false;
    if (FileDialog::file_dialog_open) {
        FileDialog::ShowFileDialog(&FileDialog::file_dialog_open, file_dialog_buffer, &closed, FileDialog::file_dialog_open_type);
    }

    if(closed) {
        vdbPath = std::string{file_dialog_buffer};
        loadState = LoadState::REQUESTED;
        closed = false;
    }

    return FileDialog::file_dialog_open;
}

void OpenVdbViewer::update(float time) {
    if(!ImGui::IsAnyItemActive() && !FileDialog::file_dialog_open){
        camera->update(time);
    }
    volumeUbo->time += time;
    volumeUbo->frame++;

    auto cam = camera->cam();
    cameraUbo->projection = cam.proj;
    cameraUbo->view = cam.view;
    cameraUbo->inverseProjection = glm::inverse(cam.proj);
    cameraUbo->inverseView = glm::inverse(cam.view);

    sliceRenderer.constants.mvp = cam.proj * cam.view * cam.model;
    sliceRenderer.constants.viewDir = camera->viewDir;

    sceneUbo->frame++;
    sceneUbo->timeDelta = time;
    sceneUbo->elapsedTime += time;
    sceneUbo->currentSample++;

    fileInfo();

    if(int(volumeUbo->time)% 5 == 0){
//        spdlog::info("ligth pos {}", light.position);
    }

    glfwSetWindowTitle(window, fmt::format("{} - fps: {}", title, framePerSecond).c_str());

    animation->update(time);
}

void OpenVdbViewer::checkAppInputs() {
    camera->processInput();

    static float pDelta = .1;
    if(ImGui::IsKeyDown(static_cast<int>(Key::W))){
        light.position.z -= pDelta;
    }
    if(ImGui::IsKeyDown(static_cast<int>(Key::S))){
        light.position.z += pDelta;
    }
    if(ImGui::IsKeyDown(static_cast<int>(Key::A))){
        light.position.x -= pDelta;
    }
    if(ImGui::IsKeyDown(static_cast<int>(Key::D))){
        light.position.x += pDelta;
    }
    if(ImGui::IsKeyDown(static_cast<int>(Key::E))) {
        light.position.y += pDelta;
    }

    if(ImGui::IsKeyDown(static_cast<int>(Key::Q))){
        light.position.y -= pDelta;
    }

    volumeUbo->lightPosition = light.position;

}

void OpenVdbViewer::cleanup() {
    VulkanBaseApp::cleanup();
}

void OpenVdbViewer::onPause() {
    VulkanBaseApp::onPause();
}

void OpenVdbViewer::fileInfo() {
    if(loadState == LoadState::REQUESTED){
        spdlog::info("requesting load of vdb file {}", vdbPath);
        loadState = LoadState::LOADING;
        animation->load(vdbPath);
    }
}

void OpenVdbViewer::newFrame() {
    camera->newFrame();
    offscreenRender();
}


void OpenVdbViewer::endFrame() {
    sceneUbo->currentSample = glm::clamp(sceneUbo->currentSample, 0, sceneUbo->numSamples - 1);

    if(camera->moved()){
        sceneUbo->currentSample = 0;
    }

    bool frameAdvanced = animation->advanceFrame(volumeDescriptor, volumeBinding);
    if(frameAdvanced) {
        auto size = glm::ivec3(animation->maxBounds() - animation->minBounds());
        auto scale = size;
        auto max = glm::max(scale.x, glm::max(scale.y, scale.z));
        light.position = (volumeUbo->boxMax + volumeUbo->boxMin) * .5f;
        light.position.y += size.y * 0.01f;
        light.scale = max * 0.01f;
        volumeUbo->boxMin = animation->minBounds();
        volumeUbo->boxMax = animation->maxBounds();
        volumeUbo->invMaxDensity = animation->inverseMaxDensity();
        volumeUbo->lightPosition = light.position;
    }

}

int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.wideLines = VK_TRUE;
        settings.deviceExtensions.push_back("VK_EXT_mutable_descriptor_type");

        std::unique_ptr<Plugin> plugin = std::make_unique<ImGuiPlugin>();

        auto app = OpenVdbViewer{ settings };
        app.addPlugin(plugin);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}