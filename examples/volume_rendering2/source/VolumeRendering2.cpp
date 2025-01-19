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
    initCamera();
    createDescriptorPool();
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
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void VolumeRendering2::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
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
    volumeDensitySetLayout =
        device.descriptorSetLayoutBuilder()
            .name("volume_density")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(poolSize + 1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(poolSize)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
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
    auto sets = descriptorPool.allocate({ volumeDensitySetLayout, volumeInfoSetLayout });
    volumeDensitySet = sets[0];
    volumeInfoSet = sets[1];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("volume_density_descriptor_set", volumeDensitySet);
    
    auto writes = initializers::writeDescriptorSets<4>();
    
    writes[0].dstSet = volumeInfoSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo vInfo{ volumeInfo, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &vInfo;

    writes[1].dstSet = volumeDensitySet;
    writes[1].dstBinding = 2;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo sceneInfo{ scene.gpu, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &sceneInfo;

    std::vector<VkDescriptorImageInfo> densityInfo(poolSize + 1);
    std::vector<VkDescriptorImageInfo> emissionInfo(poolSize);
    for(auto i = 0; i < poolSize; ++i) {
        densityInfo[i] = {pool.density[i].sampler.handle, pool.density[i].imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        emissionInfo[i] = {pool.emission[i].sampler.handle, pool.emission[i].imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    }

    densityInfo.back() = { densityVolume.sampler.handle, densityVolume.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].dstSet = volumeDensitySet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = densityInfo.size();
    writes[2].pImageInfo = densityInfo.data();

    writes[3].dstSet = volumeDensitySet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = emissionInfo.size();
    writes[3].pImageInfo = emissionInfo.data();

    device.updateDescriptorSets(writes);


}

void VolumeRendering2::updateAnimationDescriptorSets() {
    auto sets = descriptorPool.allocate(std::vector<VulkanDescriptorSetLayout>(frameCount, volumeInfoSetLayout));

    auto writes = initializers::writeDescriptorSets<>();
    std::vector<VkDescriptorBufferInfo> infos(frameCount);
    writes.resize(frameCount);
    for(auto i = 0; i < frameCount; ++ i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = sets[i];
        writes[i].dstBinding = 0;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        infos[i] = { animation[i].info, 0, VK_WHOLE_SIZE };
        writes[i].pBufferInfo = &infos[i];
        animation[i].descriptorSet = sets[i];
        device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>(fmt::format("volume_info_descriptor_set_{}", i), sets[i]);

    }

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
                    .addDescriptorSetLayout(volumeDensitySetLayout)
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
                    .addDescriptorSetLayout(volumeDensitySetLayout)
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
        scene.cpu->currentFrame = (++scene.cpu->currentFrame % animation.frameCount());
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
    info.voxelToWordTransform = glm::scale(glm::mat4{1}, glm::vec3(0.05)) * glm::translate(glm::mat4{1}, {0, -dvolume.bounds.min.y, 0}) * dvolume.voxelToWorldTransform;
//    info.voxelToWordTransform =  dvolume.voxelToWorldTransform;
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
    animation = VolumeAnimation {frameCount};

    using namespace std::chrono_literals;
    for(auto i = 0; i < frameCount; ++i) {
        auto volume = Volume::loadFromVdb(path / fmt::format("ground_explosion_{:04}.vdb", i));
        auto& dvolume = volume.begin()->second;
        Volume evolume{ };

        if(volume.contains("flames")) {
            evolume = volume["flames"];
        }

        auto center = (dvolume.bounds.min + dvolume.bounds.max) * 0.5f;
        auto offset = -center;

        VolumeInfo info{};
        info.voxelToWordTransform =
                glm::scale(glm::mat4{1}, glm::vec3(0.05))
                * glm::translate(glm::mat4{1}, offset)
                * dvolume.voxelToWorldTransform;


        info.worldToVoxelTransform = glm::inverse(info.voxelToWordTransform);
        for(auto& point : box) {
            info.bmin = glm::min(info.bmin, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
            info.bmax = glm::max(info.bmax, (info.voxelToWordTransform * glm::vec4(point, 1)).xyz());
        }

        if(pool.used < poolSize) {
            auto dData = dvolume.placeIn(dvolume.bounds.min, dvolume.bounds.max);
            auto eData = evolume.placeIn(evolume.bounds.min, evolume.bounds.max);
            textures::create(device, pool.density[pool.used], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dData.data(), dvolume.dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            textures::create(device, pool.emission[pool.used], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, eData.data(), evolume.dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
            ++pool.used;
        }
        auto bufInfo = device.createDeviceLocalBuffer(&info, sizeof(VolumeInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        animation.addFrame({ std::move(volume), std::move(bufInfo) }, 30ms);

    }
    animation.toggleLoop();
}


void VolumeRendering2::renderLevelSet(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets{};
    sets[0] = volumeDensitySet;
    sets[1] = volumeInfoSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.level_set.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.level_set.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    camera->push(commandBuffer, render.level_set.layout);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void VolumeRendering2::renderFogVolume(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets{};
    sets[0] = volumeDensitySet;
    sets[1] = animation.current().descriptorSet;

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
}

void VolumeRendering2::newFrame() {
    scene.cpu->cameraPosition = camera->position();
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