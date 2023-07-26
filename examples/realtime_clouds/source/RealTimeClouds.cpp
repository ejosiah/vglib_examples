#include "RealTimeClouds.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "implot.h"
#include "utility/dft.hpp"
#include "vulkan_image_ops.h"

RealTimeClouds::RealTimeClouds(const Settings& settings) : VulkanBaseApp("Real time clouds", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/realtime_clouds");
    fileManager.addSearchPathFront("../../examples/realtime_clouds/spv");
    fileManager.addSearchPathFront("../../examples/realtime_clouds/models");
    fileManager.addSearchPathFront("../../examples/realtime_clouds/data");
//    volumeRender.constants.width = settings.width;
//    volumeRender.constants.height = settings.height;
}

void RealTimeClouds::initApp() {
    initCamera();
    createNoiseTexture();
    createDescriptorPool();
    accStructBuilder = rt::AccelerationStructureBuilder{&device};
    createAccelerationStructure();
    initCanvas();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createNoiseGeneratorPipeline();
    createVolumeRenderPipeline();
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        generateNoise(commandBuffer);
    });
    
    auto hemisphere = primitives::sphere(1000, 1000, 30000, glm::mat4(1), glm::vec4(0), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    skyDome.vertexBuffer = device.createDeviceLocalBuffer(hemisphere.vertices.data(), BYTE_SIZE(hemisphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skyDome.indexBuffer = device.createDeviceLocalBuffer(hemisphere.indices.data(), BYTE_SIZE(hemisphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    spdlog::info("{}", hemisphere.indices.size());
    updateSun();
}

void RealTimeClouds::createAccelerationStructure(){
    imp::Box cloudBounds{glm::vec3(-1), glm::vec3(1)};

    rt::ImplicitObject boxObj;
    boxObj.numObjects = 1;
    boxObj.hitGroupId = 0;
    boxObj.aabbBuffer =
            device.createCpuVisibleBuffer(&cloudBounds, sizeof(cloudBounds)
                    , VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    auto blasId = accStructBuilder.buildBlas({ &boxObj },
                                             VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
                                             | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR ).front();

    rt::Instance instance{ blasId, 0};
    accStructBuilder.add(instance);
    asInstances = accStructBuilder.buildTlas(VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR);
}

void RealTimeClouds::createNoiseTexture() {
    Dimension3D<uint32_t> dim{NumNoiseSamples, NumNoiseSamples, NumNoiseSamples};

    textures::create(device, lowFrequencyNoiseTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, dim, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    lowFrequencyNoiseTexture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::create(device, highFrequencyNoiseTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, dim, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    highFrequencyNoiseTexture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::fromFile(device, weatherTexture, resource("weatherMap.png"));
    textures::fromFile(device, curlNoiseTexture, resource("curlNoise.png"));
}

void RealTimeClouds::initCanvas(){
    canvas = Canvas{
            this,
            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL | VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            std::nullopt,
//            resource("noise.frag.spv"),
//            VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(noiseGen.constants)}
    };
    canvas.enableBlending = true;
    canvas.init();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    blur.texture.sampler = device.createSampler(samplerInfo);

    textures::create(device, blur.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    blur.texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    blur.transferBuffer = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, blur.texture.image.size);
}

void RealTimeClouds::initCamera() {
//    OrbitingCameraSettings cameraSettings;
////    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 2.0;
//    cameraSettings.fieldOfView = 60.0f;
//    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
//
//    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 90.0f;
    cameraSettings.aspectRatio = float(width)/float(height);
    cameraSettings.zNear = 0.01;
    cameraSettings.zFar = 100000;
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), {0, 1, 0});
    inverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 3);
}


void RealTimeClouds::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 16> poolSizes{
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
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

}

void RealTimeClouds::createDescriptorSetLayouts() {
    noiseImageSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("noise_image")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    noiseTextureSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("noise_texture")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    volumeDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    blur.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("linear_blur")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void RealTimeClouds::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { noiseImageSetLayout, noiseTextureSetLayout, blur.descriptorSetLayout });
    noiseImageSet = sets[0];
    noiseTextureSet = sets[1];
    blur.descriptorSet = sets[2];
    
    auto writes = initializers::writeDescriptorSets<6>();
    
    writes[0].dstSet = noiseImageSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo noiseImageInfo{VK_NULL_HANDLE, lowFrequencyNoiseTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &noiseImageInfo;

    writes[1].dstSet = noiseImageSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo highFreqNoiseImageInfo{VK_NULL_HANDLE, highFrequencyNoiseTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &highFreqNoiseImageInfo;

    writes[2].dstSet = noiseTextureSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo noiseTextureInfo{lowFrequencyNoiseTexture.sampler, lowFrequencyNoiseTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &noiseTextureInfo;

    writes[3].dstSet = noiseTextureSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo highFreqNoiseTextureInfo{highFrequencyNoiseTexture.sampler, highFrequencyNoiseTexture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &highFreqNoiseTextureInfo;

    writes[4].dstSet = noiseTextureSet;
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo weatherInfo{ weatherTexture.sampler, weatherTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &weatherInfo;
    
    writes[5].dstSet = noiseTextureSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo curlNoiseInfo{ curlNoiseTexture.sampler, curlNoiseTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[5].pImageInfo = &curlNoiseInfo;
    
    device.updateDescriptorSets(writes);
    updateAccelerationStructureDescriptorSet();

    writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = blur.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo iInfo{blur.texture.sampler, blur.texture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &iInfo;

    writes[1].dstSet = blur.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo oInfo{VK_NULL_HANDLE, blur.texture.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &oInfo;

    device.updateDescriptorSets(writes);
}

void RealTimeClouds::updateAccelerationStructureDescriptorSet() {
    volumeDescriptorSet = descriptorPool.allocate({ volumeDescriptorSetLayout }).front();
    auto accWrites = VkWriteDescriptorSetAccelerationStructureKHR{};
    accWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accWrites.accelerationStructureCount = 1;
    accWrites.pAccelerationStructures = accStructBuilder.accelerationStructure();

    auto writes = initializers::writeDescriptorSets<3>();
    writes[0].dstSet = volumeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[0].descriptorCount = 1;
    writes[0].pNext = &accWrites;
    
    writes[1].dstSet = volumeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{ inverseCamProj, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &bufferInfo;
    
    writes[2].dstSet = volumeDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, canvas.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;

    device.updateDescriptorSets(writes);
}

void RealTimeClouds::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void RealTimeClouds::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void RealTimeClouds::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    skyDome.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("sky.vert.spv"))
                .fragmentShader(resource("sky.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
            .inputAssemblyState()
                .triangles()
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
                    .cullNone()
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
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(skyDome.constants))
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(skyDome.layout);
    //    @formatter:on
}

void RealTimeClouds::createNoiseGeneratorPipeline() {
    auto module = VulkanShaderModule{ resource("cloud.comp.spv"), device};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    cloudGen.layout = device.createPipelineLayout( {noiseImageSetLayout} );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = cloudGen.layout;

    cloudGen.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache);
}

void RealTimeClouds::createVolumeRenderPipeline() {
    auto module = VulkanShaderModule{resource("volume.comp.spv"), device};
    auto stage = initializers::shaderStage( { module, VK_SHADER_STAGE_COMPUTE_BIT});
    volumeRender.layout = device.createPipelineLayout(
            {volumeDescriptorSetLayout, noiseTextureSetLayout},
            { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(volumeRender.constants)}}
            );

    auto createInfo = initializers::computePipelineCreateInfo();
    createInfo.stage = stage;
    createInfo.layout = volumeRender.layout;

    volumeRender.pipeline = device.createComputePipeline( createInfo);

    auto linearBlurShaderModule = VulkanShaderModule{ resource("linear_blur.comp.spv"), device };
    stage = initializers::shaderStage({ linearBlurShaderModule, VK_SHADER_STAGE_COMPUTE_BIT});

    blur.layout = device.createPipelineLayout({ blur.descriptorSetLayout }, {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)}});
    auto info = initializers::computePipelineCreateInfo();


    info.stage = stage;
    info.layout = blur.layout;
    blur.pipeline = device.createComputePipeline(info);
}

void RealTimeClouds::onSwapChainDispose() {
    dispose(skyDome.pipeline);
}

void RealTimeClouds::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *RealTimeClouds::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

    renderSkyDome(commandBuffer);
    canvas.draw(commandBuffer);

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    
    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void RealTimeClouds::renderSkyDome(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyDome.pipeline);
    vkCmdPushConstants(commandBuffer, skyDome.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(skyDome.constants), &skyDome.constants);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skyDome.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skyDome.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skyDome.indexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void RealTimeClouds::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Clouds");
    ImGui::SetWindowSize({0, 0});
    ImGui::Text("Weather");
    ImGui::Indent(16);

    ImGui::Text("Sun:");
    ImGui::Indent(16);
    updateState |= ImGui::SliderFloat("Sun Azimuth", &sun.azimuth, 0, 360);
    updateState |= ImGui::SliderFloat("Sun Elevation", &sun.elevation, -90, 90);
    ImGui::Indent(-16);

    ImGui::SliderFloat("coverage", &volumeRender.constants.coverage, 0, 1);
    ImGui::SliderFloat("precipitation", &volumeRender.constants.precipitation, 0, 1);
    ImGui::SliderFloat("type", &volumeRender.constants.cloudType, 0, 1);
    ImGui::Indent(-16);
    ImGui::SliderFloat("scale", &volumeRender.constants.boxScale, 1, 10);
    ImGui::SliderFloat("eccentricity", &volumeRender.constants.eccentricity, -0.99, .99);
    ImGui::Checkbox("blur", &blur.on);
    ImGui::End();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void RealTimeClouds::update(float time) {
    volumeRender.constants.time += time;
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
        auto cam = camera->cam();

        inverseCamProj.map<glm::mat4>([&](auto ptr) {
            auto view = glm::inverse(cam.view);
            auto proj = glm::inverse(cam.proj);
            auto viewProjection = proj * view;
            *ptr = view;
            *(ptr + 1) = proj;
            *(ptr + 2) = viewProjection;
        });
    }
    skyDome.constants.eyes = camera->position();
    skyDome.constants.mvp = camera->cam().proj * camera->cam().view * camera->cam().model;
    updateSun();
    volumeRender.constants.lightPosition = skyDome.constants.sun;
    volumeRender.constants.viewPosition = skyDome.constants.eyes;
    auto label = fmt::format("{} - sun: {}", title, skyDome.constants.sun);
    glfwSetWindowTitle(window, label.c_str());
    glm::rotate(glm::mat4(1), 1.f, {1, 0, 0});
}

void RealTimeClouds::updateSun() {
    if(!updateState) return;
    updateState = false;
    auto theta = glm::radians(sun.azimuth);
    auto phi = glm::radians(sun.elevation);
    auto r = SUN_DISTANCE;
    skyDome.constants.sun.x = r * glm::cos(phi) * glm::sin(theta);
    skyDome.constants.sun.y = r * glm::cos(theta);
    skyDome.constants.sun.z = r * glm::sin(phi) * glm::cos(theta);
}

void RealTimeClouds::checkAppInputs() {
    camera->processInput();
}

void RealTimeClouds::cleanup() {
    VulkanBaseApp::cleanup();
}

void RealTimeClouds::onPause() {
    VulkanBaseApp::onPause();
}

void RealTimeClouds::generateNoise(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloudGen.pipeline);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cloudGen.layout,
                            0, 1, &noiseImageSet, 0, VK_NULL_HANDLE);

    static uint32_t nGroups = NumNoiseSamples/8;
    vkCmdDispatch(commandBuffer, nGroups, nGroups, nGroups);
}

void RealTimeClouds::newFrame() {
    renderVolume();
}

void RealTimeClouds::renderVolume() {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.image = blur.texture.image;
    
    device.computeCommandPool().oneTimeCommand([&](auto commandBuffer){

        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = volumeDescriptorSet;
        sets[1] = noiseTextureSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, volumeRender.pipeline);
        vkCmdPushConstants(commandBuffer, volumeRender.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(volumeRender.constants), &volumeRender.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, volumeRender.layout, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

        auto wg = width/32 + (width % 32);
        auto hg = height/32 + (height % 32);
        vkCmdDispatch(commandBuffer, wg, hg, 1);

        if(blur.on) {
            // blur clouds
            canvas.image.copyToBuffer(commandBuffer, blur.transferBuffer, VK_IMAGE_LAYOUT_GENERAL);
            blur.texture.image.copyFromBuffer(commandBuffer, blur.transferBuffer, VK_IMAGE_LAYOUT_GENERAL);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blur.pipeline);


            const int iterations = blur.iterations * 2;
            for (int i = 0; i < iterations; i++) {
                int horizontal = 1 - (i % 2);
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blur.layout, 0, 1,
                                        &blur.descriptorSet, 0, VK_NULL_HANDLE);
                vkCmdPushConstants(commandBuffer, blur.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int),
                                   &horizontal);
                vkCmdDispatch(commandBuffer, width, height, 1);

                if ((i + 1) < iterations) {
                    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, VK_NULL_HANDLE, 0, VK_NULL_HANDLE,
                                         1, &barrier);
                }
            }

            blur.texture.image.copyToBuffer(commandBuffer, blur.transferBuffer, VK_IMAGE_LAYOUT_GENERAL);
            canvas.image.copyFromBuffer(commandBuffer, blur.transferBuffer, VK_IMAGE_LAYOUT_GENERAL);
        }
    });
}


int main(){
    try{

        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.queueFlags |= VK_QUEUE_COMPUTE_BIT;

        auto app = RealTimeClouds{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}