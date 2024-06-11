#include "SubGroupEval.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "Barrier.hpp"

SubGroupEval::SubGroupEval(const Settings& settings) : VulkanBaseApp("Subgroup Evaluation", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/subgroup_eval");
    fileManager().addSearchPathFront("../../examples/subgroup_eval/data");
    fileManager().addSearchPathFront("../../examples/subgroup_eval/spv");
    fileManager().addSearchPathFront("../../examples/subgroup_eval/models");
    fileManager().addSearchPathFront("../../examples/subgroup_eval/textures");
}

void SubGroupEval::initApp() {
    initCamera();
    initCanvas();
    createDescriptorPool();
    createCommandPool();
    initTexture();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void SubGroupEval::initTexture() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    texture.sampler = device.createSampler(samplerInfo);
    textures::create(device, texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {32, 32, 1});
    texture.image.transitionLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);
}

void SubGroupEval::initCanvas() {
    canvas = Canvas{this, VK_IMAGE_USAGE_SAMPLED_BIT};
    canvas.init();
}

void SubGroupEval::initCamera() {
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


void SubGroupEval::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 3> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void SubGroupEval::createDescriptorSetLayouts() {
    imageDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();
}

void SubGroupEval::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { imageDescriptorSetLayout, textureDescriptorSetLayout } );
    imageDescriptorSet = sets[0];
    textureDescriptorSet = sets[1];
    
    auto writes = initializers::writeDescriptorSets<2>();
    writes[0].dstSet = imageDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ texture.sampler.handle, texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &imageInfo;

    writes[1].dstSet = textureDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &imageInfo;

    device.updateDescriptorSets(writes);
    
}

void SubGroupEval::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SubGroupEval::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SubGroupEval::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("shaders/pass_through.vert.spv"))
                    .fragmentShader(resource("shaders/pass_through.frag.spv"))
                .name("render")
                .build(render.layout);
    //    @formatter:on
}

void SubGroupEval::createComputePipeline() {
    auto module = device.createShaderModule(resource("subgroups.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout( { imageDescriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void SubGroupEval::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
}

void SubGroupEval::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *SubGroupEval::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    canvas.draw(commandBuffer, textureDescriptorSet);

    vkCmdEndRenderPass(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout.handle, 0, 1, &imageDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, texture.width/16, texture.height/16, 1);

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = texture.image;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, 0, 0, 0, 1, &barrier);
    
    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SubGroupEval::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void SubGroupEval::checkAppInputs() {
    camera->processInput();
}

void SubGroupEval::cleanup() {
    VulkanBaseApp::cleanup();
}

void SubGroupEval::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = SubGroupEval{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}