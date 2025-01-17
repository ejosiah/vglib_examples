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
    loadVolume();
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
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
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
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
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
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
    
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = volumeInfoSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo vInfo{ volumeInfo, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &vInfo;
    
    writes[1].dstSet = volumeDensitySet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo densityInfo{ densityVolume.sampler.handle, densityVolume.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &densityInfo;

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
                .layout()
                    .addDescriptorSetLayout(volumeDensitySetLayout)
                    .addDescriptorSetLayout(volumeInfoSetLayout)
                .name("level_set")
                .build(render.level_set.layout);
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

    AppContext::renderFloor(commandBuffer, *camera);
    renderLevelSet(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumeRendering2::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
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
    volume = Volume::loadFromVdb(resource("cow.vdb"));
    auto& dvolume = volume.begin()->second;
    VolumeInfo info{};
    info.voxelToWordTransform = dvolume.voxelToWordTransform;
    info.worldToVoxelTransform = dvolume.worldToVoxelTransform;
    info.bmin = dvolume.bounds.min;
    info.bmax = dvolume.bounds.max;

    volumeInfo = device.createDeviceLocalBuffer(&info, sizeof(VolumeInfo), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    textures::create(device, densityVolume, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dvolume.data.data(), dvolume.dim);

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