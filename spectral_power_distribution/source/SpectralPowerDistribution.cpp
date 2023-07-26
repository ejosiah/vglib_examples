#include "SpectralPowerDistribution.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "spectrum/spectrum.hpp"

SpectralPowerDistribution::SpectralPowerDistribution(const Settings& settings) : VulkanBaseApp("Spectral power distribution", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/spectral_power_distribution");
    fileManager.addSearchPathFront("../../examples/spectral_power_distribution/spv");
    fileManager.addSearchPathFront("../../examples/spectral_power_distribution/models");
    fileManager.addSearchPathFront("../../examples/spectral_power_distribution/textures");
    fileManager.addSearchPathFront("../../data/spd");
    fileManager.addSearchPathFront("../../data/spd/lights");
    fileManager.addSearchPathFront("../../data/spd/metals");
}

void SpectralPowerDistribution::initApp() {
    loadSpd();
    creatPatch();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}


void SpectralPowerDistribution::loadSpd() {
    spd = spectrum::loadSpd(resource("RedLaser.spd"));

    spdConstants.minValue = *std::min_element(begin(spd.values), end(spd.values));
    spdConstants.maxValue = *std::max_element(begin(spd.values), end(spd.values));
    spdConstants.minWaveLength = *std::min_element(begin(spd.lambdas), end(spd.lambdas));
    spdConstants.maxWaveLength = *std::max_element(begin(spd.lambdas), end(spd.lambdas));
    spdConstants.resolution = { width, height };

//    float range = (maxValue - minValue);
//    for(auto& v : spd.values){
//        v = (v - minValue)/range;
//        spdlog::info("{}", v);
//    }
    auto sampled = spectrum::Sampled<>::fromSampled(spd);
    spdConstants.color.rgb = sampled.toRGB() * sampled.power();
    spdConstants.color /= spdConstants.color + glm::vec4(1);
    spdlog::info("color: {}, power: {}", spdConstants.color.rgb(), sampled.power());
    spdConstants.lineResolution = 1000;
    constants.outerTessLevels[1] = spd.values.size() * 10;
    spdConstants.numBins = spd.values.size();
    spdValuesBuffer = device.createDeviceLocalBuffer(spd.values.data(), BYTE_SIZE(spd.values), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    spdWaveLengthBuffer = device.createDeviceLocalBuffer(spd.lambdas.data(), BYTE_SIZE(spd.lambdas), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    spdlog::info("push constants {}", sizeof(spdConstants) + 16);
    initCamera(spdConstants.maxValue  * 1.1f);

}

void SpectralPowerDistribution::initCamera(float height) {
    camera.model = glm::mat4(1);
    camera.view = glm::mat4(1);
    camera.proj = vkn::ortho(0.01, 1.f, 0.f, height, -1.f, 1.f);
    mvpBuffer = device.createDeviceLocalBuffer(&camera, sizeof(camera), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
}

void SpectralPowerDistribution::creatPatch() {
    std::vector<glm::vec3> points;
    points.emplace_back(0, 0, 0);
    points.emplace_back(1, 0, 0);
    points.emplace_back(0, 0, 0);
    points.emplace_back(0, 0, 0);

    isolinePatch = device.createDeviceLocalBuffer(points.data(), BYTE_SIZE(points), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    std::vector<glm::vec2> quad;
    for(auto i = 0; i < ClipSpace::Quad::positions.size(); i+= 2){
        quad.push_back(ClipSpace::Quad::positions[i]);
    }
    quadBuffer = device.createDeviceLocalBuffer(quad.data(), BYTE_SIZE(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

}


void SpectralPowerDistribution::createDescriptorPool() {
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

void SpectralPowerDistribution::createDescriptorSetLayouts() {
    spdDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("spd")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void SpectralPowerDistribution::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { spdDescriptorSetLayout });
    spdDescriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<3>();
    writes[0].dstSet = spdDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo wlInfo{ spdWaveLengthBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &wlInfo;

    writes[1].dstSet = spdDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo spdInfo{ spdValuesBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &spdInfo;

    writes[2].dstSet = spdDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo mvpInfo{ mvpBuffer, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &mvpInfo;

    device.updateDescriptorSets(writes);
}

void SpectralPowerDistribution::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SpectralPowerDistribution::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SpectralPowerDistribution::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    render.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(resource("patch.vert.spv"))
                .tessellationControlShader(resource("isolines.tesc.spv"))
                .tessellationEvaluationShader(resource("isolines.tese.spv"))
                .fragmentShader(resource("patch.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
            .inputAssemblyState()
                .patches()
            .tessellationState()
                .patchControlPoints(4)
                .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
            .multisampleState()
                .rasterizationSamples(settings.msaaSamples)
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
                    .lineWidth(2.5)
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
                    .addDescriptorSetLayout(spdDescriptorSetLayout)
                    .addPushConstantRange(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, 0, sizeof(constants))
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 16, sizeof(spdConstants))
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(render.layout);
    //    @formatter:on

    background.pipeline =
        builder
            .basePipeline(render.pipeline)
            .shaderStage().clear()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("quad.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .inputAssemblyState()
                .triangleStrip()
            .tessellationState().clear()
            .name("background")
        .build(background.layout);
}


void SpectralPowerDistribution::onSwapChainDispose() {
    dispose(render.pipeline);
}

void SpectralPowerDistribution::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *SpectralPowerDistribution::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0.8, 0.8, 0.8, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkDeviceSize offset = 0;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout, 0, 1, &spdDescriptorSet, 0, VK_NULL_HANDLE);

//    // draw background
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, background.pipeline);
//    vkCmdPushConstants(commandBuffer, background.layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, 0, sizeof(constants), &constants);
//    vkCmdPushConstants(commandBuffer, background.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 16, sizeof(spdConstants), &spdConstants);
//    vkCmdBindVertexBuffers(commandBuffer, 0, 1, quadBuffer, &offset);
//    vkCmdDraw(commandBuffer, 4, 1, 0, 0);


    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline);
    vkCmdPushConstants(commandBuffer, render.layout, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, 0, sizeof(constants), &constants);
    vkCmdPushConstants(commandBuffer, render.layout, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 16, sizeof(spdConstants), &spdConstants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, isolinePatch, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);


    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SpectralPowerDistribution::update(float time) {
}

void SpectralPowerDistribution::checkAppInputs() {
}

void SpectralPowerDistribution::cleanup() {
    VulkanBaseApp::cleanup();
}

void SpectralPowerDistribution::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.enabledFeatures.tessellationShader = VK_TRUE;
        settings.enabledFeatures.wideLines = VK_TRUE;
        settings.msaaSamples = VK_SAMPLE_COUNT_16_BIT;
        auto app = SpectralPowerDistribution{ settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}