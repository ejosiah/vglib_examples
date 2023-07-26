#include "OptixInterop.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"

OptixInterop::OptixInterop(const Settings& settings) : VulkanBaseApp("Optix Interop", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/optix_interop");
    fileManager.addSearchPathFront("../../examples/optix_interop/spv");
    fileManager.addSearchPathFront("../../examples/optix_interop/models");
    fileManager.addSearchPathFront("../../examples/optix_interop/textures");
}

void OptixInterop::initApp() {
    optix.init();
    initCamera();
    createDescriptorPool();
    initCheckerBoard();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createComputePipeline();
    createCheckerboard();
}

void OptixInterop::initCheckerBoard() {
    textures::create(device, checkerboard, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
                     {width, height, 1});

    checkerboard.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);


    byte_string alloc(width * height * sizeof(glm::vec4));
    interopBuffer = device.createDeviceLocalBuffer(alloc.data(),
                                                   BYTE_SIZE(alloc),
                                                   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

}

void OptixInterop::createCheckerboard() {
    device.graphicsCommandPool().oneTimeCommand([&](auto cb){
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout,
                                0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
        vkCmdDispatch(cb, width/32, height/32, 1);
    });
    checkerboard.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

}

void OptixInterop::initCamera() {
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


void OptixInterop::createDescriptorPool() {
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

void OptixInterop::createDescriptorSetLayouts() {
    setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void OptixInterop::updateDescriptorSets(){
    descriptorSet = descriptorPool.allocate( { setLayout }).front();
    
    auto writes = initializers::writeDescriptorSets();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, checkerboard.imageView, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &imageInfo;

    device.updateDescriptorSets(writes);
}

void OptixInterop::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OptixInterop::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OptixInterop::createComputePipeline() {
    auto module = VulkanShaderModule{resource("checkerboard.comp.spv"), device};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout( {setLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache);
}


void OptixInterop::onSwapChainDispose() {
    dispose(compute.pipeline);
}

void OptixInterop::onSwapChainRecreation() {
    updateDescriptorSets();
    createComputePipeline();
}

VkCommandBuffer *OptixInterop::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    vkCmdEndRenderPass(commandBuffer);

    copyToSwapChain(commandBuffer, checkerboard.image, imageIndex);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OptixInterop::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void OptixInterop::checkAppInputs() {
    camera->processInput();
}

void OptixInterop::cleanup() {
    VulkanBaseApp::cleanup();
}

void OptixInterop::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.width = settings.height = 1024;
        settings.depthTest = true;

        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
        settings.instanceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);

#ifdef WIN32
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME);
#endif
        settings.deviceExtensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);

        auto app = OptixInterop{ settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}