#include "Sph2D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

Sph2D::Sph2D(const Settings& settings) : VulkanBaseApp("Smoothed particle hydrodynamics", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/sph2d");
    fileManager.addSearchPathFront("../../examples/sph2d/data");
    fileManager.addSearchPathFront("../../examples/sph2d/spv");
    fileManager.addSearchPathFront("../../examples/sph2d/models");
    fileManager.addSearchPathFront("../../examples/sph2d/textures");
}

void Sph2D::initApp() {
    initSdf();
    initializeParticles();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    generatePoints();
    initPrefixSum();
    initGrid();
}

void Sph2D::initializeParticles() {
    particles.position = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec4) * particles.maxParticles);
    particles.positionReordered = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec4) * particles.maxParticles);
    particles.velocity = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec2) * particles.maxParticles);
    particles.velocityReordered = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec2) * particles.maxParticles);
    particles.forces = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec2) * particles.maxParticles);
    particles.density[0] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(float) * particles.maxParticles);
    particles.density[1] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(float) * particles.maxParticles);

    spatialHash.hashes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(int) * particles.maxParticles);
    spatialHash.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(int) * particles.maxParticles * 2 + 1);

    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());
    globals.cpu->domain.lower = glm::vec2(0);
    globals.cpu->domain.upper = glm::vec2(20);
    globals.cpu->radius = 0.05;
    globals.cpu->spacing = 0.2;
    globals.cpu->smoothingRadius = 0.1;
    setConstants(*globals.cpu);
    globals.cpu->viscousConstant = 0.99;
    globals.cpu->gravity = 0.1;
    globals.cpu->mass = 1;
    globals.cpu->gasConstant = 20;
    globals.cpu->generator = 0;
    globals.cpu->numParticles = 0;
    globals.cpu->time = fixedUpdate.period();
    globals.cpu->restDensity = 0;

    options.h = globals.cpu->smoothingRadius;
    options.k = globals.cpu->gasConstant/options.h;
    options.g = globals.cpu->gravity/options.h;

    const auto& domain = globals.cpu->domain;
    globals.cpu->projection = vkn::ortho(domain.lower.x, domain.upper.x, domain.lower.y, domain.upper.y);
}

void Sph2D::generatePoints() {
    globals.cpu->numParticles = 0;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = globalSet;
        sets[1] = sdf.descriptorSet;
        sets[2] = particles.descriptorSet;
        glm::ivec3 gc{ (globals.cpu->domain.upper - globals.cpu->domain.lower)/(globals.cpu->radius * 2), 1 };

        sdf.texture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE
                                           , VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT
                                           , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sdf.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sdf.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, 8, 8, 1);

        sdf.texture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, DEFAULT_SUB_RANGE
                , VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT
                , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generatePoints.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generatePoints.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
        addMemoryBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT);
    });
    globals.cpu->hashMapSize = globals.cpu->numParticles * 2;
    spdlog::info("{} particles generated", globals.cpu->numParticles);
}

void Sph2D::initGrid() {
    grid.constants.domain = globals.cpu->domain;
    grid.constants.spacing = globals.cpu->spacing;
    grid.canvas = Canvas{
        this,
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_FORMAT_R8G8B8A8_UNORM,
        resource("grid.vert.spv"),
        resource("grid.frag.spv"),
        { { VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(grid.constants) } }};
    grid.canvas.setConstants(&grid.constants);
    grid.canvas.init();
}

void Sph2D::initPrefixSum() {
    prefixSum = PrefixSum{ &device };
    prefixSum.init();
    prefixSum.updateDataDescriptorSets(spatialHash.counts);
}

void Sph2D::initSdf() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sdf.texture.sampler = device.createSampler(samplerInfo);
    glm::ivec3 dim{ 256, 256, 1};

    textures::create(device, sdf.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, dim);
    textures::create(device, texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, {100, 100, 1});

    texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE
            , VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT
            , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}


void Sph2D::createDescriptorPool() {
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

void Sph2D::createDescriptorSetLayouts() {
    particles.setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    particles.densitySetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    globalSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
    
    sdf.setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();            
}

void Sph2D::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({globalSetLayout, sdf.setLayout, particles.setLayout, particles.densitySetLayout, particles.densitySetLayout});
    globalSet = sets[0];
    sdf.descriptorSet = sets[1];
    particles.descriptorSet = sets[2];
    particles.densitySet[0] = sets[3];
    particles.densitySet[1] = sets[4];

    auto writes = initializers::writeDescriptorSets<13>();

    writes[0].dstSet = globalSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;

    writes[1].dstSet = sdf.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo sdfInfo{ VK_NULL_HANDLE, sdf.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &sdfInfo;

    writes[2].dstSet = sdf.descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo debugImageInfo{ VK_NULL_HANDLE, texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &debugImageInfo;

    writes[3].dstSet = sdf.descriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo sdfSamplerInfo{ sdf.texture.sampler.handle, sdf.texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[3].pImageInfo = &sdfSamplerInfo;

    writes[4].dstSet = particles.descriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo positionInfo{ particles.position, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &positionInfo;

    writes[5].dstSet = particles.descriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ particles.velocity, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &velocityInfo;

    writes[6].dstSet = particles.descriptorSet;
    writes[6].dstBinding = 2;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo forcesInfo{ particles.forces, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &forcesInfo;

    writes[7].dstSet = particles.descriptorSet;
    writes[7].dstBinding = 3;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo hashCountInfo{ spatialHash.counts, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &hashCountInfo;

    writes[8].dstSet = particles.descriptorSet;
    writes[8].dstBinding = 4;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    VkDescriptorBufferInfo hashInfo{ spatialHash.hashes, 0, VK_WHOLE_SIZE };
    writes[8].pBufferInfo = &hashInfo;

    writes[9].dstSet = particles.descriptorSet;
    writes[9].dstBinding = 5;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo positionReorderedInfo{ particles.positionReordered, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &positionReorderedInfo;

    writes[10].dstSet = particles.descriptorSet;
    writes[10].dstBinding = 6;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo velocityReorderedInfo{ particles.velocityReordered, 0, VK_WHOLE_SIZE };
    writes[10].pBufferInfo = &velocityReorderedInfo;

    writes[11].dstSet = particles.densitySet[0];
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo density0Info{ particles.density[0], 0, VK_WHOLE_SIZE };
    writes[11].pBufferInfo = &density0Info;

    writes[12].dstSet = particles.densitySet[1];
    writes[12].dstBinding = 0;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo density1Info{ particles.density[1], 0, VK_WHOLE_SIZE };
    writes[12].pBufferInfo = &density1Info;
    
    device.updateDescriptorSets(writes);
}

void Sph2D::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Sph2D::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Sph2D::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.particles.pipeline =
            builder
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .shaderStage()
                    .vertexShader(resource("particles.vert.spv"))
                    .geometryShader(resource("particles.geom.spv"))
                    .fragmentShader(resource("particles.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .compareOpAlways()
                .layout().clear()
                    .addDescriptorSetLayout(globalSetLayout)
                    .addDescriptorSetLayout(particles.setLayout)
                .name("render")
                .build(render.particles.layout);
    //    @formatter:on
}

void Sph2D::createComputePipeline() {
    auto module = device.createShaderModule(resource("point_generator.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.generatePoints.layout = device.createPipelineLayout({ globalSetLayout, sdf.setLayout, particles.setLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.generatePoints.layout.handle;

    compute.generatePoints.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("generate_points", compute.generatePoints.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_points", compute.generatePoints.pipeline.handle);


    module = device.createShaderModule(resource("generate_sdf.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.sdf.layout = device.createPipelineLayout({ globalSetLayout, sdf.setLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.sdf.layout.handle;
    compute.sdf.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("generate_sdf", compute.sdf.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_sdf", compute.sdf.pipeline.handle);


    module = device.createShaderModule(resource("compute_density.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.density.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout, particles.densitySetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.density.layout.handle;
    compute.density.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_density", compute.density.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_density", compute.density.pipeline.handle);


    module = device.createShaderModule(resource("compute_forces.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.force.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout, particles.densitySetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.force.layout.handle;
    compute.force.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("compute_forces", compute.force.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_forces", compute.force.pipeline.handle);


    module = device.createShaderModule(resource("collision.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.collision.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.collision.layout.handle;
    compute.collision.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("collision", compute.collision.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("collision", compute.collision.pipeline.handle);

    module = device.createShaderModule(resource("integrate.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.integrate.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.integrate.layout.handle;
    compute.integrate.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("integrate", compute.integrate.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("integrate", compute.integrate.pipeline.handle);

    module = device.createShaderModule(resource("hash_map.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.hashMap.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.hashMap.layout.handle;
    compute.hashMap.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("hash_map", compute.hashMap.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("hash_map", compute.hashMap.pipeline.handle);

    module = device.createShaderModule(resource("histogram.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.histogram.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.histogram.layout.handle;
    compute.histogram.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("histogram", compute.histogram.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("histogram", compute.histogram.pipeline.handle);

    module = device.createShaderModule(resource("reorder.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.reorder.layout = device.createPipelineLayout({ globalSetLayout, particles.setLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.reorder.layout.handle;
    compute.reorder.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("reorder", compute.reorder.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("reorder", compute.reorder.pipeline.handle);
}


void Sph2D::onSwapChainDispose() {
    dispose(render.particles.pipeline);
    dispose(compute.generatePoints.pipeline);
}

void Sph2D::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *Sph2D::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    if(grid.show) {
        grid.canvas.draw(commandBuffer);
    }

    renderParticles(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    runSph(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Sph2D::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Smoothed particle hydrodynamics");
    ImGui::SetWindowSize({0, 0});
    ImGui::SliderFloat("smoothing radius", &options.h, 0.1, 1.0);
    ImGui::SliderFloat("gravity", &options.g, 0, 100);
    ImGui::SliderFloat("pressure constant", &options.k, 35, 750);
    ImGui::Checkbox("Grid", &grid.show);
    ImGui::Checkbox("fixed update", &options.fixedUpdate);
    ImGui::Checkbox("accelerate", &options.accelerate);

    if(!options.start) {
        if (ImGui::Button("start")) {
            options.start = true;
        }
    }else {
        if (ImGui::Button("restart")) {
            options.reset = true;
            options.start = false;
        }
    }
    ImGui::Text("FPS %d\nFixed FPS %d\nparticles: %d", framePerSecond, fixedUpdate.framesPerSecond(), globals.cpu->numParticles);
    ImGui::End();

    plugin<ImGuiPlugin>(IM_GUI_PLUGIN).draw(commandBuffer);
}

void Sph2D::runSph(VkCommandBuffer commandBuffer) {
    if(options.start) {
        fixedUpdate([&]{
            prepareForCompute(commandBuffer);
            if(options.accelerate) {
                updateHashGrid(commandBuffer);
            }
            collisionCheck(commandBuffer);
            computeDensity(commandBuffer);
            computeForces(commandBuffer);
            integrate(commandBuffer);
            prepareForRender(commandBuffer);
        });
    }
}

void Sph2D::updateHashGrid(VkCommandBuffer commandBuffer) {
    computeHashMap(commandBuffer);
    computeHistogram(commandBuffer);
    computePartialSum(commandBuffer);
    reorder(commandBuffer);
}

void Sph2D::renderParticles(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.particles.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.particles.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 1, globals.cpu->numParticles, 0, 0);

}

void Sph2D::update(float time) {
    if(options.start) {
        fixedUpdate.update(time);
    }
}

void Sph2D::checkAppInputs() {
}

void Sph2D::cleanup() {
    VulkanBaseApp::cleanup();
}

void Sph2D::onPause() {
    VulkanBaseApp::onPause();
}

void Sph2D::newFrame() {
    if(options.reset && !ImGui::IsAnyItemActive()){
        options.reset = false;
        const auto h = options.h;
        const auto k = options.k * h;
        const auto g = options.g * h;
        globals.cpu->h = h;
        globals.cpu->spacing = 2 * h;
        setConstants(*globals.cpu);
        globals.cpu->gasConstant = k;
        globals.cpu->gravity = g;
        generatePoints();
        options.start = true;
        if(options.fixedUpdate && fixedUpdate.frequency() == 0) {
            fixedUpdate.frequency(120);
        }else if (!options.fixedUpdate && fixedUpdate.frequency() != 0){
            fixedUpdate.frequency(0);
        }
        grid.constants.domain = globals.cpu->domain;
        grid.constants.spacing = globals.cpu->spacing;
        grid.canvas.setConstants(&grid.constants);
    }
}

void Sph2D::setConstants(GlobalData &data) {
    const auto h = 2 * data.smoothingRadius;
    data.h = h;
    data.h2 = h * h;
    data._2h3 = 2 * h * h * h;
    data.kernelMultiplier = 315.f/(64.f * glm::pi<float>() * glm::pow(h, 9.f));
    data.gradientMultiplier = 45.f/(glm::pi<float>() * glm::pow(h, 6.f));
}

void Sph2D::collisionCheck(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collision.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collision.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { particles.position, particles.velocity });
}

void Sph2D::computeDensity(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    sets[2] = particles.densitySet[0];
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.density.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.density.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { particles.density[0] });
}

void Sph2D::computeForces(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    sets[2] = particles.densitySet[0];
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.force.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.force.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { particles.forces });
}

void Sph2D::integrate(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { particles.position, particles.velocity });
}

void Sph2D::prepareForCompute(VkCommandBuffer commandBuffer) {
    addBufferMemoryBarriers(commandBuffer, { particles.position }, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
}

void Sph2D::addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
}

void Sph2D::addShaderWriteToTransferReadBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
}

void Sph2D::addTransferWriteToShaderReadBarrierBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer>& buffers){
    addBufferMemoryBarriers(commandBuffer, buffers,
                            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void Sph2D::prepareForRender(VkCommandBuffer commandBuffer) {
    addBufferMemoryBarriers(commandBuffer, { particles.position }, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
}

void Sph2D::computeHashMap(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

//    vkCmdFillBuffer(commandBuffer, spatialHash.hashes, 0, VK_WHOLE_SIZE, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.hashMap.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.hashMap.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { spatialHash.hashes });
}

void Sph2D::computeHistogram(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdFillBuffer(commandBuffer, spatialHash.counts, 0, VK_WHOLE_SIZE, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.histogram.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.histogram.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { spatialHash.counts });
}

void Sph2D::computePartialSum(VkCommandBuffer commandBuffer) {
    prefixSum.inclusive(commandBuffer, spatialHash.counts, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}


void Sph2D::reorder(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = particles.descriptorSet;
    const auto gx = (globals.cpu->numParticles + workGroupSize)/workGroupSize;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.reorder.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.reorder.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gx, 1, 1);

    addShaderWriteToTransferReadBarrier(commandBuffer, { particles.positionReordered, particles.velocityReordered });
    VkBufferCopy copyRegion{0, 0, particles.positionReordered.size};
    vkCmdCopyBuffer(commandBuffer, particles.positionReordered, particles.position, 1, &copyRegion);

    copyRegion.size = particles.velocityReordered.size;
    vkCmdCopyBuffer(commandBuffer, particles.velocityReordered, particles.velocity, 1, &copyRegion);
    addTransferWriteToShaderReadBarrierBarrier(commandBuffer, { particles.position, particles.velocity });
}

void Sph2D::endFrame() {

    auto hash = [&](glm::ivec2 pid) {
        return (pid.x * 92837111) ^ (pid.y * 689287499);
    };

//    auto computeHash = [&](glm::vec2 pos) {
//        glm::ivec2 pid = glm::ivec2(pos/globals.cpu->spacing);
//        return int(abs(hash(pid)) % globals.cpu->hashMapSize);
//    };

    auto computeHash = [&](glm::vec2 pos) {
        glm::ivec2 pid = glm::ivec2(pos/globals.cpu->spacing);
        return pid.y * int(20/globals.cpu->spacing) + pid.x;
    };

//    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        runSph(commandBuffer);
//    });

//    if(options.start && options.accelerate) {
//
//        std::span<int> pHash = { reinterpret_cast<int*>(spatialHash.hashes.map()), size_t( globals.cpu->hashMapSize )  };
//        std::span<int> counts = { reinterpret_cast<int*>(spatialHash.counts.map()), size_t( globals.cpu->hashMapSize + 1) };
//        std::span<glm::vec4> pPosition { reinterpret_cast<glm::vec4*>(particles.position.map()), size_t( globals.cpu->numParticles ) };
//        for(int i = 0 ; i < 5; i++) {
//            auto pos = pPosition[i].xy();
//            auto h0 = int(pPosition[i].z);
//            auto h1 = computeHash(pos);
//            spdlog::info("{} => {}, {}", pos, h0, h1);
//        }
//
//        spatialHash.hashes.unmap();
//        spatialHash.counts.unmap();
//    }
}


int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enableBindlessDescriptors = false;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = Sph2D{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}