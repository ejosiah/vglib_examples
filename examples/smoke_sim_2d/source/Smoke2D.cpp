#include "Smoke2D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "gpu/algorithm.h"
#include "ExtensionChain.hpp"
#include <cmath>
#include <format>

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

    toggleCollider = &mapToKey(Key::B, "Toggle collider overlay", Action::detectInitialPressOnly());
}

void Smoke2D::initApp() {
    initAmbientTempBuffer();
    initFullScreenQuad();
    createDescriptorPool();
    createDescriptorSet();
    initColliderTexture();
    initSolver();
    initColliderFieldDescriptorSets();
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

void Smoke2D::initColliderTexture() {
    const auto width = static_cast<uint32_t>(fwidth);
    const auto simHeight = static_cast<uint32_t>(height);
    std::vector<glm::vec2> colliderData(
        width * simHeight,
        glm::vec2{1.0f, eular::colliderTypeValue(eular::ColliderType::Wall)});

    std::vector<glm::vec2> colliderVelocity(width * simHeight, glm::vec2{0.0f});
    colliderField.name = "smoke_collider";
    colliderVelocityField.name = "smoke_collider_velocity";
    for(auto i = 0u; i < 2; ++i) {
        textures::create(device, colliderField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT, colliderData.data(), {width, simHeight, 1u}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(glm::vec2));
        textures::create(device, colliderVelocityField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT, colliderVelocity.data(), {width, simHeight, 1u}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(glm::vec2));
        colliderField[i].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
        colliderVelocityField[i].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

        device.setName<VK_OBJECT_TYPE_IMAGE>(std::format("{}_{}", colliderField.name, i), colliderField[i].image.image);
        device.setName<VK_OBJECT_TYPE_IMAGE>(std::format("{}_{}", colliderVelocityField.name, i), colliderVelocityField[i].image.image);
    }

    computeColliderSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("smoke_collider_textures_compute")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    colliderRenderSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("smoke_collider_texture_render")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    computeColliderDescriptorSet = descriptorPool.allocate({computeColliderSetLayout}).front();
    colliderRenderDescriptorSet = descriptorPool.allocate({colliderRenderSetLayout}).front();

    auto writes = initializers::writeDescriptorSets<3>();
    VkDescriptorImageInfo colliderInfo{
        colliderField[eular::in].sampler.handle,
        colliderField[eular::in].imageView.handle,
        VK_IMAGE_LAYOUT_GENERAL
    };
    VkDescriptorImageInfo colliderVelocityInfo{
        colliderVelocityField[eular::in].sampler.handle,
        colliderVelocityField[eular::in].imageView.handle,
        VK_IMAGE_LAYOUT_GENERAL
    };

    writes[0].dstSet = computeColliderDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &colliderInfo;

    writes[1].dstSet = computeColliderDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &colliderVelocityInfo;

    writes[2].dstSet = colliderRenderDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &colliderInfo;

    device.updateDescriptorSets(writes);
}

void Smoke2D::initColliderFieldDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<12>();
    auto writeOffset = createFieldDescriptorSet(writes, 0, colliderField);
    writeOffset = createFieldDescriptorSet(writes, writeOffset, colliderVelocityField);
    writes.resize(writeOffset);
    device.updateDescriptorSets(writes);

    for(auto& write : writes) {
        if(write.pImageInfo) delete write.pImageInfo;
    }
}

uint32_t Smoke2D::createFieldDescriptorSet(std::vector<VkWriteDescriptorSet>& writes, uint32_t writeOffset, eular::Field& field) {
    auto sets = descriptorPool.allocate({fluidSolver->fieldDescriptorSetLayout(), fluidSolver->fieldDescriptorSetLayout()});

    field.descriptorSet[0] = sets[0];
    field.descriptorSet[1] = sets[1];

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 0;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 1;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 2;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 0;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 1;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 2;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    return writeOffset;
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

    colliderRender.pipeline =
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
                .addDescriptorSetLayout(colliderRenderSetLayout)
            .name("collider_render")
        .build(colliderRender.layout);
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
    dispose(colliderRender.pipeline);
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

    renderSmoke(commandBuffer);
    // fieldVisualizer.renderVectorField(commandBuffer);
    // fieldVisualizer.renderPressure(commandBuffer);
    if(showCollider) {
        fieldVisualizer.renderBoundary(commandBuffer, glm::vec4{1.0f, 0.0f, 0.0f, 0.85f}, true);
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

void Smoke2D::renderCollider(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, colliderRender.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, colliderRender.layout.handle,
                            0, 1, &colliderRenderDescriptorSet, 0, VK_NULL_HANDLE);
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

    if(toggleCollider->isPressed()) {
        showCollider = !showCollider;
        spdlog::info("Collider overlay: {}", showCollider ? "on" : "off");
    }

}

void Smoke2D::cleanup() {
    ambientTempBuffer.unmap();
}

void Smoke2D::onPause() {
    VulkanBaseApp::onPause();
}

std::vector<glm::vec4> Smoke2D::initTemperatureAndDensityField() {
    std::vector<glm::vec4> field(fwidth * height, {AMBIENT_TEMP, 0, 0, 0});

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

    return field;
}

void Smoke2D::initSolver() {
    auto temperatureAndDensityData = initTemperatureAndDensityField();
    fluidSolver =
        eular::FluidSolver::Builder{ &device, &descriptorPool }
            .gridSize({fwidth, height})
            .closedDomain()
            .openBoundaryEdges(eular::FluidSolver::BoundaryEdgeTop)
            .vorticityConfinementScale(6)
            .addQuantity(temperatureAndDensity, "temperature_and_density", temperatureAndDensityData)
            .addExternalForce(buoyancyForce())
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
