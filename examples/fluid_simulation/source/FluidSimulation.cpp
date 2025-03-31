#include "FluidSimulation.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "vulkan_image_ops.h"
#include "ExtensionChain.hpp"

FluidSimulation::FluidSimulation(const Settings& settings) : VulkanBaseApp("Fluid Simulation", settings) {
    fileManager().addSearchPath(".");
    fileManager().addSearchPath("fluid_simulation");
    fileManager().addSearchPath("fluid_simulation/spv");
    fileManager().addSearchPath("fluid_simulation/models");
    fileManager().addSearchPath("fluid_simulation/textures");
    fileManager().addSearchPath("data/shaders");
    fileManager().addSearchPath("data/models");
    fileManager().addSearchPath("data/textures");
    fileManager().addSearchPath("data");
    fileManager().addSearchPath("../data/shaders");
    fileManager().addSearchPath("../data/shaders/fluid_2d");
    constants.epsilon = 1.0f/float(width/2);    // TODO 2d epsilon
}

void FluidSimulation::initApp() {
    createSamplers();
    initFullScreenQuad();
    createDescriptorPool();
    createCommandPool();
    createDescriptorSetLayouts();

    initColorField();
    initColorQuantity();
    initFluidSolver();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}


void FluidSimulation::initFluidSolver() {
    const auto width = this->width/2;
//    const auto width = this->width;


    float dx = 1.0f/float(width);
    std::vector<glm::vec4> field;
    eular::VectorFieldSource2D field2d;
    float maxLength = MIN_FLOAT;
    constexpr auto two_pi = glm::two_pi<float>();
    for(int i = 0; i < height; i++){
        for(int j = 0; j < width; j++){
            auto x = 2 * float(j)/float(width) - 1;
            auto y = 2 * float(i)/float(height) - 1;
            glm::vec2 u{glm::sin(two_pi * y), glm::sin(two_pi * x)};
//            glm::vec2 u{1, glm::sin(two_pi * x)};
//            glm::vec2 u{x, y}; // divergent fields 1;
//            glm::vec2 u{glm::sin(two_pi * x), 0}; // divergent fields 2;
//            glm::vec2 u{y, x}; // divergent fields 3;
            maxLength = glm::max(glm::length(u), maxLength);
            field.emplace_back(u , 0, 0);
            field2d.push_back(u);
        }
    }

    auto stagingBuffer = device.createStagingBuffer(BYTE_SIZE(field));
    stagingBuffer.copy(field);

    fluidSolver = FluidSolver2D{&device, &descriptorPool, &renderPass, &fileManager(), {width, height}};
    fluidSolver.init();
    fluidSolver.set(stagingBuffer);
    fluidSolver.add(color);
    fluidSolver.dt((5.0f * dx)/maxLength);
    fluidSolver.add(userInputForce());
    fluidSolver.showVectors(true);

    glm::vec2 gridSize{ width, height};
    fluidSolver2 = eular::FluidSolver ( &device, &descriptorPool, gridSize );
    fluidSolver2.init();
    fluidSolver2.set(field2d);
    fluidSolver2.add(userInputForce2());
    fluidSolver2.add(color1);
    fluidSolver2.dt((5.0f * dx)/maxLength);
}

void FluidSimulation::initColorField() {

    auto checkerboard = [](int i, int j, float w, float h){
        auto x = 2 * (float(j)/w) - 1;
        auto y = 2 * (float(i)/h) - 1;
        return glm::vec3{
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.2) + floor((y + 1.0) / 0.2), 2.0)),
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.3) + floor((y + 1.0) / 0.3), 2.0)),
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.4) + floor((y + 1.0) / 0.4), 2.0))
        };
    };
    const auto width = this->width/2;
//    const auto width = this->width;
    std::vector<glm::vec4> field;
    std::vector<glm::vec4> allocation(width * height);
    for(auto i = 0; i < height; i++){
        for(auto j = 0; j < width; j++){
            auto color = checkerboard(j, i, float(width), float(height));
//            auto color = glm::vec3(0);
            field.emplace_back(color, 1);
        }
    }
//    field = allocation;

    textures::create(device, color.field.texture[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, color.field.texture[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));


    textures::create(device, color.source.texture[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, color.source.texture[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));

    device.setName<VK_OBJECT_TYPE_IMAGE>(fmt::format("{}_{}", "color_field", 0), color.field.texture[0].image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE>(fmt::format("{}_{}", "color_field", 1), color.field.texture[1].image.image);

    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>(fmt::format("{}_{}", "color_field", 0), color.field.texture[0].imageView.handle);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>(fmt::format("{}_{}", "color_field", 1), color.field.texture[1].imageView.handle);

    color.name = "dye";
    color.diffuseRate = diffuseRate;
//    color.update = [&](VkCommandBuffer commandBuffer, Field& field){
//        addDyeSource(commandBuffer, field, {0.004, -0.002, -0.002}, {0.2, 0.2});
//        addDyeSource(commandBuffer, field, {-0.002, -0.002, 0.004}, {0.5, 0.9});
//        addDyeSource(commandBuffer, field,  {-0.002, 0.004, -0.002}, {0.8, 0.2});
//    };
}

void FluidSimulation::initColorQuantity() {
    auto checkerboard = [](int i, int j, float w, float h){
        auto x = 2 * (float(j)/w) - 1;
        auto y = 2 * (float(i)/h) - 1;
        return glm::vec3{
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.2) + floor((y + 1.0) / 0.2), 2.0)),
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.3) + floor((y + 1.0) / 0.3), 2.0)),
                glm::step(1.0, glm::mod(floor((x + 1.0) / 0.4) + floor((y + 1.0) / 0.4), 2.0))
        };
    };
    const auto width = this->width/2;
//    const auto width = this->width;
    std::vector<glm::vec4> field;
    std::vector<glm::vec4> allocation(width * height);
    for(auto i = 0; i < height; i++){
        for(auto j = 0; j < width; j++){
            auto color = checkerboard(j, i, float(width), float(height));
//            auto color = glm::vec3{0};
            field.emplace_back(color, 1);
        }
    }
//    field = allocation;

    textures::create(device, color1.field[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, color1.field[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , field.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));


    textures::create(device, color1.source[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));
    textures::create(device, color1.source[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT
            , allocation.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
            , sizeof(float));

    device.setName<VK_OBJECT_TYPE_IMAGE>(fmt::format("{}_{}", "color1_field", 0), color1.field[0].image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE>(fmt::format("{}_{}", "color1_field", 1), color1.field[1].image.image);

    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>(fmt::format("{}_{}", "color1_field", 0), color1.field[0].imageView.handle);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>(fmt::format("{}_{}", "color1_field", 1), color1.field[1].imageView.handle);

    color1.field[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    color1.field[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    color1.source[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    color1.source[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    color1.name = "dye";
    color1.diffuseRate = diffuseRate;
//    color1.update = [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
//        addDyeSource1(commandBuffer, field, gc, {0.004, -0.002, -0.002}, {0.2, 0.2});
//        addDyeSource1(commandBuffer, field, gc, {-0.002, -0.002, 0.004}, {0.5, 0.9});
//        addDyeSource1(commandBuffer, field, gc,  {-0.002, 0.004, -0.002}, {0.8, 0.2});
//    };
}

void FluidSimulation::initFullScreenQuad() {
    auto quad = ClipSpace::Quad::positions;
    screenQuad.vertices = device.createDeviceLocalBuffer(quad.data(), BYTE_SIZE(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void FluidSimulation::createDescriptorPool() {
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

void FluidSimulation::createDescriptorSetLayouts() {
    textureSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_set")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();
}

void FluidSimulation::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void FluidSimulation::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void FluidSimulation::createComputePipeline() {
    auto module = device.createShaderModule(resource("force.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    forceGen2.layout = device.createPipelineLayout( fluidSolver2.forceFieldSetLayouts(),
                                                  { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(forceGen.constants) } } );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = forceGen2.layout.handle;
    forceGen2.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("force_generator", forceGen2.pipeline.handle);

    // dye source
    module = device.createShaderModule(resource("color_source.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    dyeSource.compute.layout = device.createPipelineLayout( fluidSolver2.sourceFieldSetLayouts(),
                                                    { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dyeSource.constants) } } );

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = dyeSource.compute.layout.handle;
    dyeSource.compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("dye_source", dyeSource.compute.pipeline.handle);
}


void FluidSimulation::createRenderPipeline() {
    //    @formatter:off
    auto& simRenderPass = fluidSolver.renderPass;
    auto builder = device.graphicsPipelineBuilder();
    render.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(resource("pass_through.vert.spv"))
                .fragmentShader(resource("pass_through.frag.spv"))
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
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(render.layout);

    screenQuad.pipeline =
        builder
            .basePipeline(render.pipeline)
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("render.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
            .layout().clear()
                .addDescriptorSetLayout(fluidSolver.textureSetLayout)
                .addDescriptorSetLayout(fluidSolver2._fieldDescriptorSetLayout)
            .name("fullscreen_quad")
        .build(screenQuad.layout);

    forceGen.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("force.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayouts({ fluidSolver.textureSetLayout })
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(forceGen.constants))
            .renderPass(simRenderPass)
            .name("force_generator")
        .build(forceGen.layout);

    dyeSource.pipeline =
        builder
            .shaderStage()
                .fragmentShader(resource("color_source.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayouts({ fluidSolver.textureSetLayout })
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dyeSource.constants))
            .renderPass(simRenderPass)
            .name("color_source")
        .build(dyeSource.layout);

    //    @formatter:on
}

void FluidSimulation::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(forceGen2.pipeline);
}

void FluidSimulation::onSwapChainRecreation() {
    initFluidSolver();
    createRenderPipeline();
}

VkCommandBuffer *FluidSimulation::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

//    fluidSolver.renderVectorField(commandBuffer);
    renderColorField(commandBuffer);
//    renderDebugField(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);
    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void FluidSimulation::renderColorField(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = color.field.descriptorSet[in];
    sets[1] = color1.field.descriptorSet[in];
//    sets[0] = fluidSolver.divergenceField.descriptorSet[in];
//    sets[1] = fluidSolver2._divergenceField.descriptorSet[in];

//    sets[0] = fluidSolver.pressureField.descriptorSet[in];
//    sets[1] = fluidSolver2._pressureField.descriptorSet[in];

//    sets[0] = fluidSolver.vectorField.descriptorSet[in];
//    sets[1] = fluidSolver2._combinedVectorField.descriptorSet[in];

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad.vertices, &offset);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screenQuad.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screenQuad.layout.handle
            , 0, COUNT(sets), sets.data(), 0
            , VK_NULL_HANDLE);
//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, screenQuad.layout.handle
//            , 0, 1, &fluidSolver.pressureField.descriptorSet[1], 0
//            , VK_NULL_HANDLE);

    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void FluidSimulation::update(float time) {
    auto title = fmt::format("{} - fps {}", this->title, framePerSecond);
    glfwSetWindowTitle(window, title.c_str());
    runSimulation();
}

void FluidSimulation::runSimulation() {
//    static int count = 0;
//    if(count > 0) return;
//    ++count;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        fluidSolver.runSimulation(commandBuffer);
        fluidSolver2.runSimulation(commandBuffer);
    });

}

ExternalForce FluidSimulation::userInputForce() {
    return [&](VkCommandBuffer commandBuffer, VkDescriptorSet descriptorSet){
        forceGen.constants.dt = fluidSolver.dt();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, forceGen.pipeline.handle);
        vkCmdPushConstants(commandBuffer, forceGen.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(forceGen.constants), &forceGen.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, forceGen.layout.handle, 0, 1, &descriptorSet, 0, VK_NULL_HANDLE);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);
        forceGen.constants.force.x = 0;
        forceGen.constants.force.y = 0;
    };
}

eular::ExternalForce FluidSimulation::userInputForce2() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        forceGen2.constants.dt = fluidSolver2.dt();
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, forceGen2.pipeline.handle);
        vkCmdPushConstants(commandBuffer, forceGen2.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(forceGen2.constants), &forceGen2.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, forceGen2.layout.handle, 0, COUNT(forceFieldSets), forceFieldSets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer,  gc.x, gc.y, gc.z);
        forceGen2.constants.force.x = 0;
        forceGen2.constants.force.y = 0;
    };
}

void FluidSimulation::addDyeSource(VkCommandBuffer commandBuffer, Field &field, glm::vec3 color, glm::vec2 source) {

    dyeSource.constants.dt = fluidSolver.dt();
    dyeSource.constants.color.rgb = color;
    dyeSource.constants.source = source;
    fluidSolver.withRenderPass(commandBuffer, field.framebuffer[out], [&](auto commandBuffer){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dyeSource.pipeline.handle);
        vkCmdPushConstants(commandBuffer, dyeSource.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dyeSource.constants), &dyeSource.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, dyeSource.layout.handle, 0, 1, &field.descriptorSet[in], 0, VK_NULL_HANDLE);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    });
    field.swap();
}

void FluidSimulation::addDyeSource1(VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc, glm::vec3 color, glm::vec2 source) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = field.descriptorSet[0];
    sets[1] = field.descriptorSet[1];

    dyeSource.constants.dt = fluidSolver2.dt();
    dyeSource.constants.color.rgb = color;
    dyeSource.constants.source = source;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dyeSource.compute.pipeline.handle);
    vkCmdPushConstants(commandBuffer, dyeSource.compute.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dyeSource.constants), &dyeSource.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dyeSource.compute.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer,  gc.x, gc.y, gc.z);

    field.swap();
}

void FluidSimulation::checkAppInputs() {
    static bool initialPress = true;
    if(mouse.left.held){
        if(initialPress){
            initialPress = false;
            auto mousePos = glm::vec3(mouse.position, 1);
            glm::vec4 viewport{0, 0, swapChain.width(), swapChain.height()};
            forceGen.constants.center =  glm::unProject(mousePos, glm::mat4(1), glm::mat4(1), viewport).xy();
        }
    }else if(mouse.left.released){
        initialPress = true;
        auto mousePos = glm::vec3(mouse.position, 1);
        glm::vec4 viewport{0, 0, swapChain.width(), swapChain.height()};
        auto pos =  glm::unProject(mousePos, glm::mat4(1), glm::mat4(1), viewport).xy();

        auto displacement = pos - forceGen.constants.center;
        forceGen.constants.force = displacement;
        forceGen.constants.center = .5f * forceGen.constants.center + .5f;
    }
    forceGen2.constants = forceGen.constants;
}

void FluidSimulation::cleanup() {
    // TODO save pipeline cache
    VulkanBaseApp::cleanup();
}

void FluidSimulation::onPause() {
    VulkanBaseApp::onPause();
}

void FluidSimulation::createSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST ;

    valueSampler = device.createSampler(samplerInfo);

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    linearSampler = device.createSampler(samplerInfo);
}

void FluidSimulation::beforeDeviceCreation() {
    VkDescriptorSetLayoutBinding binding {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };

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

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1200;
        settings.height = 600;
        settings.depthTest = true;
        settings.vSync = true;
//        spdlog::set_level(spdlog::level::err);
        auto app = FluidSimulation{settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}