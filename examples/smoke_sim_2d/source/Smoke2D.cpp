#include "Smoke2D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "gpu/algorithm.h"
#include "ExtensionChain.hpp"
#include <cmath>

Smoke2D::Smoke2D(const Settings& settings) :
        VulkanBaseApp("2D Smoke Simulation", settings)
    , fwidth(static_cast<int>(600))
{
    fileManager().addSearchPath(".");
    fileManager().addSearchPath("smoke_sim_2d");
    fileManager().addSearchPath("smoke_sim_2d/spv");
    fileManager().addSearchPath("smoke_sim_2d/models");
    fileManager().addSearchPath("smoke_sim_2d/textures");
    fileManager().addSearchPath("../data/shaders");
    fileManager().addSearchPath("../data/models");
    fileManager().addSearchPath("../data/textures");
    fileManager().addSearchPath("../data");

    toggleBoundary = &mapToKey(Key::B, "Toggle boundary overlay", Action::detectInitialPressOnly());
}

void Smoke2D::initApp() {
    initAmbientTempBuffer();
    initFullScreenQuad();
    createDescriptorPool();
    createDescriptorSet();
    initBoundaryTexture();
    initSolver();
    initFieldVisualizer();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void Smoke2D::initAmbientTempBuffer() {
    auto temp = AMBIENT_TEMP;
    ambientTempBuffer = device.createCpuVisibleBuffer(&temp, sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    tempField = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
            , VMA_MEMORY_USAGE_CPU_TO_GPU, fwidth * height * sizeof(float));
    debugBuffer = device.createStagingBuffer(sizeof(glm::vec4) * fwidth * height);
    ambientTemp = reinterpret_cast<float*>(ambientTempBuffer.map());
    temps = reinterpret_cast<float*>(tempField.map());
}

void Smoke2D::initFullScreenQuad() {
    auto quad = ClipSpace::Quad::positions;
    screenQuad = device.createDeviceLocalBuffer(quad.data(), BYTE_SIZE(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void Smoke2D::createDescriptorPool() {
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

void Smoke2D::createDescriptorSet() {
    ambientTempSet =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    ambientTempDescriptorSet = descriptorPool.allocate({ ambientTempSet }).front();
}

void Smoke2D::updateDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = ambientTempDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo inInfo{ tempField, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &inInfo;

    writes[1].dstSet = ambientTempDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo outInfo{ambientTempBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &outInfo;

    device.updateDescriptorSets(writes);

}

void Smoke2D::initBoundaryTexture() {
    const auto width = static_cast<uint32_t>(fwidth);
    const auto simHeight = static_cast<uint32_t>(height);
    constexpr auto topCornerOpening = 10u;
    constexpr auto sourceOpeningPadding = 0;
    const auto sourceCenterX = static_cast<int>(std::lround(emitter.constants.location.x * static_cast<float>(width - 1)));
    const auto sourceRadiusPixels = static_cast<int>(std::ceil(std::sqrt(emitter.constants.radius) * static_cast<float>(width)));
    const auto sourceOpeningHalfWidth = sourceRadiusPixels + sourceOpeningPadding / 2;

    std::vector<float> boundary(width * simHeight, 0.0f);
    for(auto y = 0u; y < simHeight; ++y) {
        for(auto x = 0u; x < width; ++x) {
            const auto sideWall = (x == 0 || x == width - 1) && y >= topCornerOpening;
            const auto sourceOpening = std::abs(static_cast<int>(x) - sourceCenterX) <= sourceOpeningHalfWidth;
            const auto bottomWall = y == simHeight - 1; // && !sourceOpening;

            if(sideWall || bottomWall) {
                boundary[y * width + x] = 1.0f;
            }
        }
    }

    textures::create(device, boundaryTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT,
                     boundary.data(), {width, simHeight, 1u}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    device.setName<VK_OBJECT_TYPE_IMAGE>("smoke_boundary_texture", boundaryTexture.image.image);

    computeBoundarySetLayout =
        device.descriptorSetLayoutBuilder()
            .name("smoke_boundary_texture_compute")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    boundaryRenderSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("smoke_boundary_texture_render")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    computeBoundaryDescriptorSet = descriptorPool.allocate({computeBoundarySetLayout}).front();
    boundaryRenderDescriptorSet = descriptorPool.allocate({boundaryRenderSetLayout}).front();

    auto writes = initializers::writeDescriptorSets<2>();
    VkDescriptorImageInfo boundaryInfo{
        boundaryTexture.sampler.handle,
        boundaryTexture.imageView.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };

    writes[0].dstSet = computeBoundaryDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &boundaryInfo;

    writes[1].dstSet = boundaryRenderDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &boundaryInfo;

    device.updateDescriptorSets(writes);
}

void Smoke2D::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Smoke2D::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Smoke2D::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    temperatureRender.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("temperature.frag.spv"))
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
                    .addDescriptorSetLayout(fluidSolver->fieldDescriptorSetLayout())
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(temperatureRender.constants))
                .renderPass(renderPass)
                .subpass(0)
                .name("temperature_render")
                .pipelineCache(pipelineCache)
            .build(temperatureRender.layout);

    smokeRender.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("smoke_render.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayout(fluidSolver->fieldDescriptorSetLayout())
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(smokeRender.constants))
            .name("smoke_render")
        .build(smokeRender.layout);

    boundaryRender.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("boundary_render.frag.spv"))
            .depthStencilState()
                .disableDepthWrite()
                .disableDepthTest()
            .colorBlendState()
                .attachment()
                    .clear()
                    .enableBlend()
                    .srcColorBlendFactor().srcAlpha()
                    .dstColorBlendFactor().oneMinusSrcAlpha()
                    .srcAlphaBlendFactor().one()
                    .dstAlphaBlendFactor().oneMinusSrcAlpha()
                    .add()
            .layout().clear()
                .addDescriptorSetLayout(boundaryRenderSetLayout)
            .name("boundary_render")
        .build(boundaryRender.layout);
    //    @formatter:on
}

void Smoke2D::createComputePipeline() {
    auto module = device.createShaderModule(resource("smoke_source.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    auto createInfo = initializers::computePipelineCreateInfo();
    auto sourceSets = fluidSolver->sourceFieldSetLayouts();
    sourceSets.push_back(ambientTempSet);
    emitter.compute.layout = device.createPipelineLayout(
            sourceSets,
            {{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(emitter.constants) }});
    createInfo.stage = stage;
    createInfo.layout = emitter.compute.layout.handle;
    emitter.compute.pipeline = device.createComputePipeline(createInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("smoke_source_layout", emitter.compute.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("smoke_source_pipeline", emitter.compute.pipeline.handle);

    // smoke decay
    module = device.createShaderModule(resource("decay_smoke.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    smokeDecay.compute.layout = device.createPipelineLayout(
             sourceSets,
            {{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(smokeDecay.constants) }});
    createInfo.stage = stage;
    createInfo.layout = smokeDecay.compute.layout.handle;
    smokeDecay.compute.pipeline = device.createComputePipeline(createInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("decay_smoke_layout", smokeDecay.compute.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("decay_smoke_pipeline", smokeDecay.compute.pipeline.handle);

    // buoyancy force
    module = device.createShaderModule(resource("buoyancy_force.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    auto forceSets = fluidSolver->forceFieldSetLayouts();
    forceSets.push_back(fluidSolver->fieldDescriptorSetLayout());
    forceSets.push_back(ambientTempSet);
    buoyancyForceGen.compute.layout = device.createPipelineLayout(
            forceSets,
            {{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(buoyancyForceGen.constants) }});
    createInfo.stage = stage;
    createInfo.layout = buoyancyForceGen.compute.layout.handle;
    buoyancyForceGen.compute.pipeline = device.createComputePipeline(createInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("buoyancy_force_layout", buoyancyForceGen.compute.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("buoyancy_force_pipeline", buoyancyForceGen.compute.pipeline.handle);

    // copy temparature
    module = device.createShaderModule(resource("copy_temperature_field.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    copyTemperatureField.layout = device.createPipelineLayout({ fluidSolver->fieldDescriptorSetLayout(), ambientTempSet } );
    createInfo.stage = stage;
    createInfo.layout = copyTemperatureField.layout.handle;
    copyTemperatureField.pipeline = device.createComputePipeline(createInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("copy_temperature_layout", copyTemperatureField.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("copy_temperature_pipeline", copyTemperatureField.pipeline.handle);
}


void Smoke2D::onSwapChainDispose() {
    dispose(temperatureRender.pipeline);
    dispose(smokeRender.pipeline);
    dispose(boundaryRender.pipeline);
}

void Smoke2D::onSwapChainRecreation() {
    createRenderPipeline();
}

VkCommandBuffer *Smoke2D::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    fluidSolver->runSimulation(commandBuffer);
    fieldVisualizer.update(commandBuffer);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {1, 1, 1, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    fieldVisualizer.renderVectorField(commandBuffer);
    // renderSmoke(commandBuffer);
    if(showBoundary) {
        renderBoundary(commandBuffer);
    }
    // renderTemperature(commandBuffer);
    // fieldVisualizer.renderStreamLines(commandBuffer);
    // fieldVisualizer.renderPressure(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Smoke2D::renderTemperature(VkCommandBuffer commandBuffer) {
    const auto set = temperatureAndDensity.field.descriptorSet[in];

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, temperatureRender.pipeline.handle);
    vkCmdPushConstants(commandBuffer, temperatureRender.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(temperatureRender.constants), &temperatureRender.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, temperatureRender.layout.handle
            , 0, 1, &set, 0
            , VK_NULL_HANDLE);

    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Smoke2D::renderSmoke(VkCommandBuffer commandBuffer) {
    const auto set = temperatureAndDensity.field.descriptorSet[in];

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, smokeRender.pipeline.handle);
    vkCmdPushConstants(commandBuffer, smokeRender.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(smokeRender.constants), &smokeRender.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, smokeRender.layout.handle
            , 0, 1, &set, 0
            , VK_NULL_HANDLE);

    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Smoke2D::renderBoundary(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, boundaryRender.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, boundaryRender.layout.handle,
                            0, 1, &boundaryRenderDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Smoke2D::update(float time) {
    auto title = fmt::format("{}, temperature {:.3f}, fps {}", this->title, *ambientTemp, framePerSecond);
    glfwSetWindowTitle(window, title.c_str());
//    gpu::average(tempField, ambientTempBuffer);
//    *ambientTemp /= static_cast<float>(fwidth * height);
}

void Smoke2D::checkAppInputs() {
    VulkanBaseApp::checkAppInputs();

    if(toggleBoundary->isPressed()) {
        showBoundary = !showBoundary;
        spdlog::info("Boundary overlay: {}", showBoundary ? "on" : "off");
    }

}

void Smoke2D::cleanup() {
    ambientTempBuffer.unmap();
}

void Smoke2D::onPause() {
    VulkanBaseApp::onPause();
}

void Smoke2D::initTemperatureAndDensityField() {
    std::vector<glm::vec4> field(fwidth * height, {AMBIENT_TEMP, 0, 0, 0});

    textures::create(device, temperatureAndDensity.field[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {fwidth, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, temperatureAndDensity.field[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {fwidth, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));

    std::vector<glm::vec4> allocation(fwidth * height);
    textures::create(device, temperatureAndDensity.source[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {fwidth, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, temperatureAndDensity.source[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {fwidth, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));

    temperatureAndDensity.field[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    temperatureAndDensity.field[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    temperatureAndDensity.source[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    temperatureAndDensity.source[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    temperatureAndDensity.name = "temperature_and_density";
    temperatureAndDensity.field.name = "temperature_and_density";
    temperatureAndDensity.source.name = "temperature_and_density_source";
//    temperatureAndDensity.diffuseRate = 1e-7;
    temperatureAndDensity.diffuseRate = 0;
    temperatureAndDensity.update = [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
        emitSmoke(commandBuffer, field, gc);
    };

    temperatureAndDensity.postAdvectActions.emplace_back([&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
        return decaySmoke(commandBuffer, field, gc);
    });

    temperatureAndDensity.postAdvectActions.emplace_back(
        [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
            updateAmbientTemperature(commandBuffer, field, gc);
            return false;
        }
    );
}

void Smoke2D::initSolver() {
    initTemperatureAndDensityField();
    fluidSolver =
        eular::FluidSolver::Builder{ &device, &descriptorPool }
            .gridSize({fwidth, height})
            .boundary(computeBoundaryDescriptorSet)
            // .ensureBoundaryCondition(false)
            .vorticityConfinementScale(6)
            .add(temperatureAndDensity)
            .add(buoyancyForce())
            .useConjugateGradientSolver()
            // .poissonIterations(100)
            .dt(TIME_STEP)
        .build();

}

void Smoke2D::emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    emitter.constants.dt = fluidSolver->dt();
    emitter.constants.time = fluidSolver->elapsedTime();

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = field.descriptorSet[in];
    sets[1] = field.descriptorSet[out];

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, emitter.compute.pipeline.handle);
    vkCmdPushConstants(commandBuffer, emitter.compute.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(emitter.constants), &emitter.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, emitter.compute.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    field.swap();
}

bool Smoke2D::decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    smokeDecay.constants.dt = fluidSolver->dt();

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = field.descriptorSet[in];
    sets[1] = field.descriptorSet[out];

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, smokeDecay.compute.pipeline.handle);
    vkCmdPushConstants(commandBuffer, smokeDecay.compute.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(smokeDecay.constants), &smokeDecay.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, smokeDecay.compute.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    return true;
}


eular::ExternalForce Smoke2D::buoyancyForce() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        static std::array<VkDescriptorSet, 4> sets;
        sets[0] = forceFieldSets[in];
        sets[1] = forceFieldSets[out];
        sets[2] = temperatureAndDensity.field.descriptorSet[in];
        sets[3] = ambientTempDescriptorSet;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, buoyancyForceGen.compute.pipeline.handle);
        vkCmdPushConstants(commandBuffer, buoyancyForceGen.compute.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(buoyancyForceGen.constants), &buoyancyForceGen.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, buoyancyForceGen.compute.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    };
}

void Smoke2D::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->maintenance4 = VK_TRUE;
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;


    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;
}

void Smoke2D::updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = field.descriptorSet[in];
    sets[1] = ambientTempDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, copyTemperatureField.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, copyTemperatureField.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
}

void Smoke2D::initFieldVisualizer() {
    fieldVisualizer = FieldVisualizer{
            &device, &descriptorPool, &renderPass, fluidSolver->fieldDescriptorSetLayout(),
            { fwidth, height }, { fwidth, height }
    };

    fieldVisualizer.init();
    fieldVisualizer.set(fluidSolver.get());
}


int main(){
    try{
        fs::current_path("../../../../examples/");

        Settings settings;
        settings.enableResize = false;
        settings.width = 600;
        settings.height = 1000;
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        auto app = Smoke2D{ settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
