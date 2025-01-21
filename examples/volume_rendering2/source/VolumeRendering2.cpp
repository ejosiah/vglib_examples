#include "VolumeRendering2.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

VolumeRendering2::VolumeRendering2(const Settings& settings) : VulkanBaseApp("Volume Rendering", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("data");
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("volume_rendering2");
    fileManager().addSearchPathFront("volume_rendering2/data");
    fileManager().addSearchPathFront("volume_rendering2/spv");
    fileManager().addSearchPathFront("volume_rendering2/models");
    fileManager().addSearchPathFront("volume_rendering2/textures");
}

void VolumeRendering2::initApp() {
    openvdb::initialize();
    initScene();
    loadVolume();
    loadAnimation();
    initTextureCopyData();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    updateAnimationDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void VolumeRendering2::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 1000.0f;
    cameraSettings.offsetDistance = 1.f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void VolumeRendering2::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, poolSize * 2);
}

void VolumeRendering2::beforeDeviceCreation() {
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
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL_GRAPHICS });

}

void VolumeRendering2::createDescriptorPool() {
    constexpr uint32_t maxSets = 1000;
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


void VolumeRendering2::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void VolumeRendering2::createDescriptorSetLayouts() {
    volumeInfoSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("volume_info")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
    
}

void VolumeRendering2::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ volumeInfoSetLayout });
    volumeInfoSet = sets[0];


    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = volumeInfoSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo vInfo{ volumeInfo, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &vInfo;

    writes[1].dstSet = bindlessDescriptor.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo sceneInfo{ scene.gpu, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &sceneInfo;

    device.updateDescriptorSets(writes);

    for(auto i = 0u; i < poolSize; ++i) {
        bindlessDescriptor.update({ &pool.density[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, i});
        bindlessDescriptor.update({ &pool.emission[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, poolSize + i});
    }

}

void VolumeRendering2::updateAnimationDescriptorSets() {
    animation.metadata.descriptorSet = descriptorPool.allocate( {volumeInfoSetLayout} ).front();

    auto writes = initializers::writeDescriptorSets<>();
    VkDescriptorBufferInfo infos{ animation.metadata.info, 0, VK_WHOLE_SIZE };
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = animation.metadata.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &infos;

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("volume_info_descriptor_set", animation.metadata.descriptorSet);

    device.updateDescriptorSets(writes);
}

void VolumeRendering2::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumeRendering2::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VolumeRendering2::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.level_set.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("volume.vert.spv"))
                    .fragmentShader(resource("level_set.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .compareOpLess()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_ALL_GRAPHICS))
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(volumeInfoSetLayout)
                .name("level_set")
                .build(render.level_set.layout);

        render.fog.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("volume.vert.spv"))
                    .fragmentShader(resource("fog.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
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
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_ALL_GRAPHICS))
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(volumeInfoSetLayout)
                .name("fog")
                .build(render.fog.layout);
    //    @formatter:on
}


void VolumeRendering2::onSwapChainDispose() {
    dispose(render.level_set.pipeline);
}

void VolumeRendering2::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *VolumeRendering2::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    transferFramesToGpu(commandBuffer);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.2, 0.2, 0.2, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

//    AppContext::renderFloor(commandBuffer, *camera);
//    renderLevelSet(commandBuffer);
    renderFogVolume(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumeRendering2::update(float time) {
    static int count = 0;
    camera->update(time);
    auto cam = camera->cam();

    if(count > 10 && animation.next(time)) {
        scene.cpu->currentFrame = (++scene.cpu->currentFrame % poolSize);
        pool.used--;

        static auto freeSlots = 0;
        freeSlots = poolSize - pool.used;

        if(freeSlots == batchSize) {
            for(auto i = 0; i < freeSlots; ++i) {
                regions[i].srcBuffer = animation[pendingFrameOffset].density;
                regions[i].eSrcBuffer = animation[pendingFrameOffset].emission;
                regions[i].dstImage = pool.density[pool.freeHead].image;
                regions[i].eDstImage = pool.emission[pool.freeHead].image;

                pendingFrameOffset = (++pendingFrameOffset) % animation.frameCount();
                pool.freeHead = (++pool.freeHead) % poolSize;
                pool.used++;
            }
            copyPending = true;
        }
    }
    setTitle(fmt::format("{}, FPS - {}, cam: {}", title, framePerSecond, camera->position()));
    ++count;
}

void VolumeRendering2::checkAppInputs() {
    camera->processInput();
}

void VolumeRendering2::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void VolumeRendering2::onPause() {
    VulkanBaseApp::onPause();
}

void VolumeRendering2::loadVolume() {
//    volume = Volume::loadFromVdb(resource("smoke_002_0.10_0067.vdb"));
    volume = Volume::loadFromVdb(resource("ground_explosion_0100.vdb"));
    auto& dvolume = volume.begin()->second;
    VolumeInfo info{};
    info.voxelToWordTransform = glm::scale(glm::mat4{1}, glm::vec3(0.05)) * glm::translate(glm::mat4{1}, {0, -dvolume.bounds.min.y, 0}) * dvolume.voxelToLocalTransform;
//    info.voxelToWordTransform =  dvolume.voxelToLocalTransform;
    info.worldToVoxelTransform = glm::inverse(info.voxelToWordTransform);
    for(auto& point : box) {
        info.bmin = glm::min(info.bmin, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
        info.bmax = glm::max(info.bmax, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
    }
    spdlog::info("volume: [{}, {}]", info.bmin, info.bmax);
    volumeInfo = device.createDeviceLocalBuffer(&info, sizeof(VolumeInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    auto data = dvolume.placeIn(dvolume.bounds.min, dvolume.bounds.max);
    textures::create(device, densityVolume, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, data.data(), dvolume.dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

}

void VolumeRendering2::loadAnimation() {
//    fs::path path{R"(C:\Users\joebh\OneDrive\media\volumes\_VDB-Smoke-Pack\smoke_044_Low_Res)"};
    fs::path path{R"(C:\Users\joebh\OneDrive\media\volumes\GroundExplosionVDB\ground_explosion\ground_explosion_VDB)"};
    animation = VolumeAnimation {aframeCount};

    glm::vec3 bmin{MAX_FLOAT};
    glm::vec3 bmax{MIN_FLOAT};

    std::vector<VolumeSet> volumes;
    for(auto i = 0; i < aframeCount; ++i) {
        auto volume = Volume::loadFromVdb(path / fmt::format("ground_explosion_{:04}.vdb", i));
        auto& dvolume = volume.begin()->second;
        auto& evolume = volume["flames"];

        volumes.push_back(std::move(volume));
        if(dvolume.numVoxels == 0  && evolume.numVoxels == 0);

        bmin = glm::min(dvolume.bounds.min, bmin);
        bmax = glm::max(dvolume.bounds.max, bmax);

        bmin = glm::min(evolume.bounds.min, bmin);
        bmax = glm::max(evolume.bounds.max, bmax);

    }

    auto invMaxAxis = 1.f/(bmax - bmin);
    auto model = glm::mat4{1};

    model = glm::scale(model, invMaxAxis);
    model = glm::translate(model, -bmin);

    auto localToVoxelTransform = model;
    auto voxelToLocalTransform = glm::inverse(model);

    auto center = (bmin + bmax) * 0.5f;
    auto offset = -center;

    auto localToWorld = glm::mat4{1};
    localToWorld = glm::scale(localToWorld, glm::vec3(0.05));
    localToWorld = glm::translate(localToWorld, offset);

    VolumeInfo info{};
    info.worldToVoxelTransform = localToVoxelTransform * glm::inverse(localToWorld);
    info.voxelToWordTransform = localToWorld * voxelToLocalTransform;
    for(auto& point : box) {
        info.bmin = glm::min(info.bmin, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
        info.bmax = glm::max(info.bmax, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
    }

    const auto tmin = (info.worldToVoxelTransform * glm::vec4(info.bmin, 1)).xyz();
    const auto tmax = (info.worldToVoxelTransform * glm::vec4(info.bmax, 1)).xyz();

    assert(glm::all(glm::epsilonEqual(tmin, glm::vec3(0), 0.0001f)));
    assert(glm::all(glm::epsilonEqual(tmax, glm::vec3(1), 0.0001f)));

    const auto voxelSize = volumes.front().begin()->second.voxelSize;
    const auto ibmax = glm::ivec3(glm::floor(bmax/voxelSize));
    const auto ibmin = glm::ivec3(glm::floor(bmin/voxelSize));
    const auto dim = ibmax - ibmin + 1;
    info.dimensions = dim;
    animation.metadata.dimensions = dim;
    animation.metadata.info = device.createDeviceLocalBuffer(&info, sizeof(VolumeInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    using namespace std::chrono_literals;
    for(auto i = 0; i < aframeCount; ++i) {
        auto volume = volumes[i];
        auto& dvolume = volume["density"];
        auto& evolume = volume["flames"];

        auto dData = dvolume.placeIn(bmin, bmax);
        auto eData = evolume.placeIn(bmin, bmax);

        if(pool.used < poolSize) {
//            auto eData = std::vector<float>{1.0f};
            textures::create(device, pool.density[pool.used], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dData.data(), dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            textures::create(device, pool.emission[pool.used], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, eData.data(), dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            ++pool.used;
        }
        VulkanBuffer densityBuffer = device.createStagingBuffer(BYTE_SIZE(dData));
        densityBuffer.copy(dData);

        VulkanBuffer emissionBuffer = device.createStagingBuffer(BYTE_SIZE(eData));
        emissionBuffer.copy(eData);

        animation.addFrame({ std::move(densityBuffer), std::move(emissionBuffer)}, 30ms);

    }
    pendingFrameOffset = pool.used;
    animation.toggleLoop();
}


void VolumeRendering2::renderLevelSet(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets{};
    sets[0] = bindlessDescriptor.descriptorSet;
    sets[1] = volumeInfoSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.level_set.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.level_set.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    camera->push(commandBuffer, render.level_set.layout);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void VolumeRendering2::renderFogVolume(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets{};
    sets[0] = bindlessDescriptor.descriptorSet;
    sets[1] = animation.metadata.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.fog.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.fog.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    camera->push(commandBuffer, render.fog.layout, VK_SHADER_STAGE_ALL_GRAPHICS);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void VolumeRendering2::initScene() {
    auto initData = SceneData{};
    initData.extinction = initData.scattering + initData.absorption;
    scene.gpu = device.createCpuVisibleBuffer(&initData, sizeof(SceneData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    scene.cpu = reinterpret_cast<SceneData*>(scene.gpu.map());
    scene.cpu->texturePoolSize = poolSize;
}

void VolumeRendering2::newFrame() {
    scene.cpu->cameraPosition = camera->position();
}

void VolumeRendering2::initTextureCopyData() {
    for(auto i = 0; i < batchSize * 2; ++i) {
        if(i < batchSize) {
            auto &copyCmd = regions[i];
            copyCmd.region.bufferOffset = 0;
            copyCmd.region.bufferRowLength = 0;
            copyCmd.region.bufferImageHeight = 0;
            copyCmd.region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copyCmd.region.imageSubresource.mipLevel = 0;
            copyCmd.region.imageSubresource.baseArrayLayer = 0;
            copyCmd.region.imageSubresource.layerCount = 1;
            copyCmd.region.imageOffset = {0, 0, 0};
            copyCmd.region.imageExtent = {animation.metadata.dimensions.x, animation.metadata.dimensions.y,
                                          animation.metadata.dimensions.z};
        }

        auto& tBarrier = imageBarriersToTransfer[i];
        tBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        tBarrier.srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        tBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        tBarrier.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        tBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        tBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        tBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        tBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        tBarrier.subresourceRange.baseMipLevel = 0;
        tBarrier.subresourceRange.levelCount = 1;
        tBarrier.subresourceRange.baseArrayLayer = 0;
        tBarrier.subresourceRange.layerCount = 1;

        auto& sBarrier = imageBarriersToShaderRead[i];
        sBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        sBarrier.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        sBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sBarrier.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        sBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        sBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        sBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        sBarrier.subresourceRange.baseMipLevel = 0;
        sBarrier.subresourceRange.levelCount = 1;
        sBarrier.subresourceRange.baseArrayLayer = 0;
        sBarrier.subresourceRange.layerCount = 1;
    }
    tDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    tDependencyInfo.imageMemoryBarrierCount = batchSize * 2;
    tDependencyInfo.pImageMemoryBarriers = imageBarriersToTransfer.data();

    sDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    sDependencyInfo.imageMemoryBarrierCount = batchSize * 2;
    sDependencyInfo.pImageMemoryBarriers = imageBarriersToShaderRead.data();
}

void VolumeRendering2::transferFramesToGpu(VkCommandBuffer commandBuffer) {
    if(!copyPending) return;

    copyPending = false;

    for(auto i = 0; i < batchSize; ++i) {
        imageBarriersToTransfer[i].image = regions[i].dstImage;
        imageBarriersToTransfer[i + batchSize].image = regions[i].eDstImage;
        imageBarriersToShaderRead[i].image = regions[i].dstImage;
        imageBarriersToShaderRead[i + batchSize].image = regions[i].eDstImage;
    }
    vkCmdPipelineBarrier2(commandBuffer, &tDependencyInfo);

    for(auto& copy : regions) {
        vkCmdCopyBufferToImage(commandBuffer, copy.srcBuffer, copy.dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy.region);
        vkCmdCopyBufferToImage(commandBuffer, copy.eSrcBuffer, copy.eDstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy.region);
    }

    vkCmdPipelineBarrier2(commandBuffer, &sDependencyInfo);
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1024;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = VolumeRendering2{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}