#include "RtxAmbientOcclusion.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"

RtxAmbientOcclusion::RtxAmbientOcclusion(const Settings& settings) : VulkanBaseApp("Ray Traced Ambient Occlusion", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("raytraced_ambient_occlusion");
    fileManager().addSearchPathFront("raytraced_ambient_occlusion/data");
    fileManager().addSearchPathFront("raytraced_ambient_occlusion/spv");
    fileManager().addSearchPathFront("raytraced_ambient_occlusion/models");
    fileManager().addSearchPathFront("raytraced_ambient_occlusion/textures");
}

void RtxAmbientOcclusion::initApp() {
    initCamera();
    createNoiseTextures();
    initGBuffer();
    initDenoiser();
    initOffscreen();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    loadModel();
    initAsBuilder();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    compute = std::make_unique<ComputePipelines>(&device, pipelineMetaData());
    compute->createPipelines();
}

void RtxAmbientOcclusion::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.zFar = 10000;
    cameraSettings.orbitMaxZoom = 10000;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->zoomDelta = 10;
}

void RtxAmbientOcclusion::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void RtxAmbientOcclusion::beforeDeviceCreation() {
    AppContext::addExtensions(deviceCreateNextChain);
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto features12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    features12->timelineSemaphore = VK_TRUE;
}

void RtxAmbientOcclusion::createDescriptorPool() {
    constexpr uint32_t maxSets = 300;
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

void RtxAmbientOcclusion::createDescriptorSetLayouts() {
    gBufferDescriptorSetLayout = 
        device.descriptorSetLayoutBuilder()
            .name("g_buffer_descriptor_set_layout")
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

    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    imageDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("image_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    accStructDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void RtxAmbientOcclusion::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { gBufferDescriptorSetLayout, imageDescriptorSetLayout, accStructDescriptorSetLayout, textureDescriptorSetLayout });
    gBuffer.descriptorSet = sets[0];
    ambientDescriptorSet = sets[1];
    accStructDescriptorSet = sets[2];
    noise.descriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<7>();
    
    writes[0].dstSet = gBuffer.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo positionInfo{ gBuffer.position.sampler.handle, gBuffer.position.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &positionInfo;

    writes[1].dstSet = gBuffer.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{ gBuffer.normal.sampler.handle, gBuffer.normal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1].pImageInfo = &normalInfo;

    writes[2].dstSet = gBuffer.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo aoInfo{ gBuffer.ambientOcclusion.sampler.handle, gBuffer.ambientOcclusion.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &aoInfo;

    writes[3].dstSet = gBuffer.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo depthInfo{ gBuffer.depth.sampler.handle, gBuffer.depth.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[3].pImageInfo = &depthInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = noise.descriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo noiseInfo{ noise.texture.sampler.handle, noise.texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[4].pImageInfo = &noiseInfo;


    writes[5].dstSet = ambientDescriptorSet;
    writes[5].dstBinding = 0;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo aoImageeInfo{ nullptr, gBuffer.ambientOcclusion.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[5].pImageInfo = &aoImageeInfo;


    auto accWrites = VkWriteDescriptorSetAccelerationStructureKHR{};
    accWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accWrites.accelerationStructureCount = 1;
    accWrites.pAccelerationStructures = accStructBuilder.accelerationStructure();

    writes[6].dstSet = accStructDescriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[6].descriptorCount = 1;
    writes[6].pNext = &accWrites;

    device.updateDescriptorSets(writes);

}

void RtxAmbientOcclusion::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount * CBG::Count);
}

void RtxAmbientOcclusion::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void RtxAmbientOcclusion::createRenderPipeline() {
    //    @formatter:off
        render.gBuffer.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("g_buffer.vert.spv"))
                    .fragmentShader(resource("g_buffer.frag.spv"))
                .colorBlendState()
                    .attachments(2)
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                    .depthAttachment(VK_FORMAT_D16_UNORM)
                .name("g_buffer")
            .build(render.gBuffer.layout);

        render.quad.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(gBufferDescriptorSetLayout)
                .name("render")
            .build(render.quad.layout);
    //    @formatter:on
}


void RtxAmbientOcclusion::onSwapChainDispose() {
    dispose(render.gBuffer.pipeline);
    dispose(render.quad.pipeline);
}

void RtxAmbientOcclusion::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *RtxAmbientOcclusion::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex * CBG::Count + CBG::Render];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0.3, 0.3, 0.3);

    renderToSwapChain([&]{
        renderScene(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    writeToGBuffer(commandBuffer);
    computeAO(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void RtxAmbientOcclusion::renderScene(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.quad.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.quad.layout.handle, 0, 1, &gBuffer.descriptorSet, 0, nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void RtxAmbientOcclusion::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    setTitle(fmt::format("{}, FPS: {}", title, framePerSecond));
}

void RtxAmbientOcclusion::checkAppInputs() {
    camera->processInput();
}

void RtxAmbientOcclusion::cleanup() {
    AppContext::shutdown();
    dispose(denoiser);
    dispose(optix);
}

void RtxAmbientOcclusion::onPause() {
    VulkanBaseApp::onPause();
}

void RtxAmbientOcclusion::loadModel() {
    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;
//    phong::load(resource("lte-orb.obj"), device, descriptorPool, model, info);
    phong::load(resource("ico_sphere.obj"), device, descriptorPool, model, info);
//    phong::load(resource("leaving_room/living_room.obj"), device, descriptorPool, model, info);
//    phong::load(resource("conference.obj"), device, descriptorPool, model, info);
    phong::load(resource("plane.obj"), device, descriptorPool, plane, info);
}


void RtxAmbientOcclusion::initAsBuilder() {
    accStructBuilder = rt::AccelerationStructureBuilder{&device};
    accStructBuilder.usage = rt::AsUsage::RayQuery;
    rt::MeshObjectInstance modelInstance{ { &model } };
    rt::MeshObjectInstance planeInstance { { &plane }};

    accStructBuilder.add({ modelInstance, planeInstance });
    accStructBuilder.buildTlas();
}

void RtxAmbientOcclusion::initGBuffer() {
    textures::color(device, gBuffer.color, glm::vec3(1), {width, height});
    textures::create(device, gBuffer.position, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.normal, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.ambientOcclusion, VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {width, height, 1});
    textures::create(device, gBuffer.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    gBuffer.ambientOcclusion.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void RtxAmbientOcclusion::initOffscreen() {
    renderInfo = {
        .colorAttachments = {
            { gBuffer.position.imageView, VK_FORMAT_R32G32B32A32_SFLOAT },
            { gBuffer.normal.imageView, VK_FORMAT_R32G32B32A32_SFLOAT },
        },
        .depthAttachment = {{ gBuffer.depth.imageView, VK_FORMAT_D16_UNORM }},
        .renderArea = { width, height}
    };
}

void RtxAmbientOcclusion::writeToGBuffer(VkCommandBuffer commandBuffer) {
    Barriers::push(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    Barriers::flush(commandBuffer);
    offscreen.render(commandBuffer, renderInfo, [&]{
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.gBuffer.pipeline.handle);
        camera->push(commandBuffer, render.gBuffer.layout);
        model.draw(commandBuffer);
        plane.draw(commandBuffer);
    });
    auto dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    Barriers::push(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, dstStage , VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    Barriers::flush(commandBuffer);
}

void RtxAmbientOcclusion::createNoiseTextures() {
    noise.texture.sampler = createNoiseSampler();
    if(noise.blueNoise) {
        std::vector<std::string> paths;
        for(auto i = 0; i < NoiseCount; ++i) {
            paths.push_back(resource(std::format("fast_noise/128_128/cosine/cosine_{}.png", i)));
        }
        textures::fromFile(device, noise.texture, paths);
    }else {
        auto Xi = rng(0.f, 1.f);
        const auto gr = golden_ratio_conjugate<float>();

        auto cosine_sample_hemisphere = [](auto u) {
            const auto r = std::sqrt(u.x);
            const auto phi = 2.f * glm::pi<decltype(u.x)>() * u.y;

            auto x = r * cos(phi);
            auto y = r * sin(phi);
            auto z = glm::max(0.f, 1.f - x*x - y*y);

            return glm::vec3{x, y, z};
        };

        std::vector<std::vector<glm::vec4>> noiseData(NoiseCount);

        std::generate(noiseData.begin(), noiseData.end(), [&]{
           std::vector<glm::vec4> layer(128 * 128);

           std::generate(layer.begin(), layer.end(), [&]{
               auto sample = .5f + .5f  *cosine_sample_hemisphere(glm::vec2(Xi(), Xi()));
               return glm::vec4{sample, 0 };
           });
           return layer;
        });

        std::vector<void *> data = map_range(noiseData, [](auto& d){ return reinterpret_cast<void*>(d.data()); });
        textures::createTextureArray(device, noise.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, data, {128, 128, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, 4);

    }

}

VulkanSampler RtxAmbientOcclusion::createNoiseSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode =  VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.maxLod = 1;

    return device.createSampler(samplerInfo);}

std::vector<PipelineMetaData> RtxAmbientOcclusion::pipelineMetaData() {
    return {
            {
                .name = "compute_ao",
                .shadePath = resource("compute_ao.comp.spv"),
                .layouts = { &gBufferDescriptorSetLayout, &imageDescriptorSetLayout, &accStructDescriptorSetLayout, &textureDescriptorSetLayout },
                .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants) } }
            },
    };}

void RtxAmbientOcclusion::computeAO(VkCommandBuffer commandBuffer) {
    const auto gx = (width + 7)/8;
    const auto gy = (height + 7)/8;
    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = gBuffer.descriptorSet;
    sets[1] = ambientDescriptorSet;
    sets[2] = accStructDescriptorSet;
    sets[3] = noise.descriptorSet;
    VkClearColorValue clearValue{0.f, 0.f, 0.f, 0.f};
    vkCmdClearColorImage(commandBuffer, gBuffer.ambientOcclusion.image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &DEFAULT_SUB_RANGE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline("compute_ao"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout("compute_ao"), 0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute->layout("compute_ao"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barriers::push(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    Barriers::flush(commandBuffer);

}

void RtxAmbientOcclusion::endFrame() {
    denoise();
    constants.frame++;
}

void RtxAmbientOcclusion::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Options");
    ImGui::SetWindowSize({0, 0});

    ImGui::SliderInt("samples", &constants.sampleCount, 1, NoiseCount);
    ImGui::SliderFloat("radius", &constants.radius, 0.01, 10);

    ImGui::Checkbox("denoise", &shouldDenoise);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void RtxAmbientOcclusion::initDenoiser() {
    optix = std::make_shared<OptixContext>();
    VkDeviceSize size = gBuffer.ambientOcclusion.image.size;
    auto bufferUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    cuda::Buffer colorBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, gBuffer.ambientOcclusion.image.size)};
    cuda::Buffer normalBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, gBuffer.normal.image.size)};
    cuda::Buffer albedoBuffer{ device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, gBuffer.color.image.size)};

    std::vector<cuda::Buffer> outputs;
    outputs.emplace_back(device, device.createExportableBuffer(bufferUsage, VMA_MEMORY_USAGE_CPU_TO_GPU, gBuffer.ambientOcclusion.image.size));

    VulkanDenoiser::Data data = VulkanDenoiser::Data{
            static_cast<uint32_t>(swapChain.width()),
            static_cast<uint32_t>(swapChain.height()),
            colorBuffer,
            albedoBuffer,
            normalBuffer,
            outputs,
    };
    VulkanDenoiser::Settings settings{};
    denoiser = std::make_unique<VulkanDenoiser>( optix, data, settings);
    denoiseSemaphore = cuda::Semaphore{device};
}

void RtxAmbientOcclusion::denoise() {
    if(!shouldDenoise) return;

    // TODO compute AO and then wait before denoising

    auto commandBuffer = commandBuffers[currentImageIndex * CBG::Count + CBG::PreDenoise];
    auto beginInfo = initializers::commandBufferBeginInfo();

    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    denoiser->update(commandBuffer,
                     gBuffer.ambientOcclusion.image,
                     gBuffer.color.image,
                     gBuffer.normal.image);
    vkEndCommandBuffer(commandBuffer);

    auto waitValue = fenceValue;
    fenceValue++;
    denoiseTimelineInfo.waitSemaphoreValueCount = 1;
    denoiseTimelineInfo.pWaitSemaphoreValues = &waitValue;
    denoiseTimelineInfo.signalSemaphoreValueCount = 1;
    denoiseTimelineInfo.pSignalSemaphoreValues = &fenceValue;

    VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submitInfo.pNext = &denoiseTimelineInfo;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = denoiseSemaphore.vk;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(device.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);

    // Wait for Vulkan to copy image to denoise buffers
    cudaExternalSemaphoreWaitParams waitParams{};
    waitParams.flags = 0;
    waitParams.params.fence.value = fenceValue;
    cudaWaitExternalSemaphoresAsync(&denoiseSemaphore.cu, &waitParams, 1, nullptr);
    denoiser->exec();

    cudaExternalSemaphoreSignalParams signalParams{};
    signalParams.flags = 0;
    signalParams.params.fence.value = ++fenceValue;
    cudaSignalExternalSemaphoresAsync(&denoiseSemaphore.cu, &signalParams, 1, optix->m_cudaStream);


    commandBuffer = commandBuffers[currentImageIndex * CBG::Count + CBG::PostDenoise];
    beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    denoiser->copyOutputTo(commandBuffer, gBuffer.ambientOcclusion.image);
    vkEndCommandBuffer(commandBuffer);

    waitValue = fenceValue;
    ++fenceValue;
    denoiseTimelineInfo.waitSemaphoreValueCount = 1;
    denoiseTimelineInfo.pWaitSemaphoreValues = &waitValue;
    denoiseTimelineInfo.signalSemaphoreValueCount = 1;
    denoiseTimelineInfo.pSignalSemaphoreValues = &fenceValue;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    submitInfo.pNext = &denoiseTimelineInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = denoiseSemaphore.vk;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = denoiseSemaphore.vk;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkQueueSubmit(device.queues.graphics, 1, &submitInfo, VK_NULL_HANDLE);

    VkSemaphoreWaitInfo waitInfo;
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.pNext = NULL;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = denoiseSemaphore.vk;
    waitInfo.pValues = &fenceValue;

    vkWaitSemaphores(device.logicalDevice, &waitInfo, UINT64_MAX);

}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.depthTest = true;
        settings.vSync = false;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);

        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);

#ifdef WIN32
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#endif

        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = RtxAmbientOcclusion{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}