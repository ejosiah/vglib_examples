#include "Collision3D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"

Collision3D::Collision3D(const Settings& settings) : VulkanBaseApp("3D collision", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("collision_3d");
    fileManager().addSearchPathFront("collision_3d/data");
    fileManager().addSearchPathFront("collision_3d/spv");
    fileManager().addSearchPathFront("collision_3d/models");
    fileManager().addSearchPathFront("collision_3d/textures");
}

void Collision3D::initApp() {
    domain = { {-4, 0, -4}, {4, 4, 4} };
    createShapes();
    initScratchBuffer();
    initObjects();
    initParticleEmitters();
    initSphereEmitters();
    initCamera();
    initCanvas();
    createInverseCam();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void Collision3D::initCamera() {
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

void Collision3D::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void Collision3D::beforeDeviceCreation() {
    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
        devFeatures12.value()->shaderOutputViewportIndex = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        devFeatures12.shaderOutputViewportIndex = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }


    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
        devFeatures13.value()->maintenance4 = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        devFeatures13.maintenance4 = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void Collision3D::createDescriptorPool() {
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


void Collision3D::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void Collision3D::createDescriptorSetLayouts() {
    objects.setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
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
            .binding(7)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(9)
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
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    emitterSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(2)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}

void Collision3D::updateDescriptorSets(){
    // objects
    auto sets = descriptorPool.allocate({
        globalSetLayout, objects.setLayout, emitterSetLayout,
    });
    globalSet = sets[0];
    objects.descriptorSet = sets[1];
    emitterDescriptorSet = sets[2];

    auto writes = initializers::writeDescriptorSets<11>();

    writes[0].dstSet = globalSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;

    // Objects

    writes[1].dstSet = objects.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 2;
    std::array<VkDescriptorBufferInfo, 2> positionInfo{{
        { objects.position[0], 0, VK_WHOLE_SIZE }, { objects.position[1], 0, VK_WHOLE_SIZE }
    }};
    writes[1].pBufferInfo = positionInfo.data();

    writes[2].dstSet = objects.descriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ objects.velocity, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &velocityInfo;

    writes[3].dstSet = objects.descriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo cvInfo{ objects.correctionVector, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &cvInfo;

    writes[4].dstSet = objects.descriptorSet;
    writes[4].dstBinding = 3;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo radiusInfo{ objects.radius, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &radiusInfo;

    writes[5].dstSet = objects.descriptorSet;
    writes[5].dstBinding = 4;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo cellIdInfo{ objects.cellIds, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &cellIdInfo;

    writes[6].dstSet = objects.descriptorSet;
    writes[6].dstBinding = 5;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo attributesInfo{ objects.attributes, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &attributesInfo;

    writes[7].dstSet = objects.descriptorSet;
    writes[7].dstBinding = 6;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ objects.counts, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &countsInfo;

    writes[8].dstSet = objects.descriptorSet;
    writes[8].dstBinding = 7;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexInfo{ objects.cellIndexArray, 0, VK_WHOLE_SIZE };
    writes[8].pBufferInfo = &cellIndexInfo;

    writes[9].dstSet = objects.descriptorSet;
    writes[9].dstBinding = 8;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchCmdInfo{ objects.dispatchBuffer, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &dispatchCmdInfo;


    // emiiter
    std::vector<VkDescriptorBufferInfo> emitterInfo {
            { emitters.particle, 0, VK_WHOLE_SIZE },
            { emitters.sphere, 0, VK_WHOLE_SIZE },
    };
    writes[10].dstSet = emitterDescriptorSet;
    writes[10].dstBinding = 0;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = emitterInfo.size();
    writes[10].pBufferInfo = emitterInfo.data();

    device.updateDescriptorSets(writes);

}

void Collision3D::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Collision3D::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void Collision3D::initCanvas() {
    canvas = Canvas{ this, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_FORMAT_R8G8B8A8_UNORM};
    canvas.init();
    std::vector<unsigned char> checkerboard(width * height * 4);
    textures::checkerboard1(checkerboard.data(), {width, height});
    const auto stagingBuffer = device.createCpuVisibleBuffer(checkerboard.data(), BYTE_SIZE(checkerboard), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        textures::transfer(commandBuffer, stagingBuffer, canvas.image, {width, height}, VK_IMAGE_LAYOUT_GENERAL);
    });
}

void Collision3D::createInverseCam() {
    inverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 2);
}

void Collision3D::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.bounds.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("bounds.vert.spv"))
                    .fragmentShader(resource("bounds.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .name("bounds")
            .build(render.bounds.layout);

        render.shape.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("particles.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .layout()
                    .addDescriptorSetLayout(globalSetLayout)
                    .addDescriptorSetLayout(objects.setLayout)
                .name("particles")
            .build(render.shape.layout);
    //    @formatter:on
}

void Collision3D::createComputePipeline() {
    auto module = device.createShaderModule(resource("emitter.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.emitter.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, emitterSetLayout} );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.emitter.layout.handle;

    compute.emitter.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("particle_emitter", compute.emitter.pipeline.handle);

    // sphere emitter
    module = device.createShaderModule(resource("sphere_emitter.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.sphereEmitter.layout = device.createPipelineLayout( {  globalSetLayout, objects.setLayout, emitterSetLayout} );
    computeCreateInfo.layout = compute.sphereEmitter.layout.handle;
    compute.sphereEmitter.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sphere_emitter", compute.sphereEmitter.pipeline.handle);


    // integrator
    module = device.createShaderModule(resource("integrate.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.integrate.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.integrate.layout.handle;
    compute.integrate.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("integrate", compute.integrate.pipeline.handle);

    // bounds check
    module = device.createShaderModule(resource("bounds_check.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.boundsCheck.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.boundsCheck.layout.handle;
    compute.boundsCheck.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("bounds_check", compute.boundsCheck.pipeline.handle);

    // corrections
    module = device.createShaderModule(resource("apply_correction.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.correction.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.correction.layout.handle;
    compute.correction.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("apply_correction", compute.correction.pipeline.handle);

    // velocity update
    module = device.createShaderModule(resource("update_velocity.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.velocity.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.velocity.layout.handle;
    compute.velocity.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("update_velocity", compute.velocity.pipeline.handle);

}


void Collision3D::onSwapChainDispose() {
    dispose(render.bounds.pipeline);
    dispose(render.shape.pipeline);
    dispose(compute.emitter.pipeline);
}

void Collision3D::onSwapChainRecreation() {
    initCanvas();
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *Collision3D::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    runSimulation(commandBuffer);

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

    renderBounds(commandBuffer);
    renderParticles(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

//    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Collision3D::renderBounds(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.bounds.pipeline.handle);
    camera->push(commandBuffer, render.bounds.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &bounds.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, bounds.indexes,  0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, bounds.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Collision3D::renderParticles(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.shape.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.shape.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, render.shape.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &ball.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, ball.indexes,  0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, ball.indexes.sizeAs<uint32_t>(), globals.cpu->numObjects, 0, 0, 0);
}

void Collision3D::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    globals.cpu->frame++;
    globals.cpu->time = time;
    setTitle(fmt::format("{}, {} active objects", title, globals.cpu->numObjects));

    static float elapsedTime = 0;
    elapsedTime += time;

    if(elapsedTime > 10) {
        pauseSim = false;
    }
}

void Collision3D::checkAppInputs() {
    camera->processInput();
}

void Collision3D::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void Collision3D::onPause() {
    VulkanBaseApp::onPause();
}

void Collision3D::createShapes() {
    auto scale = (domain.upper - domain.lower) * 0.5f;
    auto transform = glm::translate(glm::mat4{1}, {0, scale.y, 0});
    transform = glm::scale(transform, scale);
    auto wall = primitives::cube(glm::vec4(0.6), transform);

    bounds.vertices = device.createDeviceLocalBuffer(wall.vertices.data(), BYTE_SIZE(wall.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    bounds.indexes = device.createDeviceLocalBuffer(wall.indices.data(), BYTE_SIZE(wall.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    auto sphere = primitives::sphere(50, 50, 1.f, glm::mat4{1}, {1, 0, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    ball.vertices = device.createDeviceLocalBuffer(sphere.vertices.data(), BYTE_SIZE(sphere.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    ball.indexes = device.createDeviceLocalBuffer(sphere.indices.data(), BYTE_SIZE(sphere.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Collision3D::initObjects() {
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData3D));
    globals.cpu = reinterpret_cast<GlobalData3D*>(globals.gpu.map());

    globals.cpu->domain.lower = domain.lower;
    globals.cpu->domain.upper = domain.upper;
    globals.cpu->gravity = {0, -9.8f, 0};
    globals.cpu->numObjects = 0;
    globals.cpu->segmentSize = 2;
    globals.cpu->frame = 0;
    globals.cpu->time = fixedUpdate.period();

    globals.cpu->halfSpacing = objects.defaultRadius;
    globals.cpu->spacing = glm::sqrt(2.f) * objects.defaultRadius * 3;
    glm::uvec2 dim{((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) + 1.f };
    globals.cpu->gridSize = objects.gridSize = dim.x * dim.y;

    static constexpr VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY;
    uint32_t numParticle = objects.maxParticles;

    objects.position[0] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.position[1] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.correctionVector = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.velocity = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec3));
    objects.radius = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(float));
    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle * 4);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle* 4);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute3D) * numParticle * 4);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute3D) * numParticle * 4);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, sizeof(uint32_t) * (objects.gridSize + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(CellInfo3D) * objects.gridSize);
    objects.cellIndexStaging = reserve(sizeof(CellInfo3D) * objects.gridSize);
    objects.bitSet = reserve(sizeof(uint32_t) * std::max(objects.gridSize, numParticle));
    objects.compactIndices = reserve(sizeof(uint32_t) * (objects.gridSize + 1));
    objects.indices = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(uint32_t) * 4);


    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memoryUsage, Dispatch::Size, "dispatch_cmd_buffer"); ;


}

BufferRegion Collision3D::reserve(VkDeviceSize size) {
    size = alignedSize(size, device.getLimits().minStorageBufferOffsetAlignment);
    assert(scratchPad.offset + size <= scratchPad.buffer.size);
    auto start = scratchPad.offset;
    scratchPad.offset += size;
    return { &scratchPad.buffer, start, scratchPad.offset };
}

void Collision3D::initScratchBuffer() {
    scratchPad.buffer = device.createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_GPU_TO_CPU, (20 * (1 << 20)), "scratch_buffer");
}

void Collision3D::initParticleEmitters() {
    const auto& domain = globals.cpu->domain;
    const auto radius = objects.defaultRadius;
    const auto diameter = radius * 2;
    globals.cpu->numEmitters = 1;

    Emitter3D prototype{
        .origin = { 0, 5, 3.5 },
        .direction = {0, -1, -1},
        .radius = radius,
        .offset = 1.25,
        .speed = 2,
        .spreadAngleRad = 0,
        .maxNumberOfParticlePerSecond = 10,
        .maxNumberOfParticles = static_cast<int>(glm::max(1u, objects.maxParticles / globals.cpu->numEmitters)),
        .firstFrameTimeInSeconds = 0,
        .currentTime = 0,
        .numberOfEmittedParticles = 0,
        .disabled = false,
    };

    std::vector<Emitter3D> emits{};
    for(auto i = 0; i < globals.cpu->numEmitters; ++i){
        Emitter3D emitter = prototype;
//        emitter.origin.x = domain.upper.x - 2.f * (radius + radius * static_cast<float>(i));
        emits.push_back(emitter);
    }
    emitters.particle = device.createDeviceLocalBuffer(emits.data(), BYTE_SIZE(emits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void Collision3D::initSphereEmitters() {
    const auto& domain = globals.cpu->domain;
    const auto radius = objects.defaultRadius;
    const auto diameter = radius * 2;
    globals.cpu->numSphereEmitters = 1;

    Emitter3D prototype{
        .origin = { 0, 5, 3.5 },
        .direction = {0, -1, -1},
        .radius = radius,
        .offset = 1.25,
        .speed = 2,
        .spreadAngleRad = 0,
        .maxNumberOfParticlePerSecond = 10,
        .maxNumberOfParticles = static_cast<int>(glm::max(1u, objects.maxParticles / globals.cpu->numEmitters)),
        .firstFrameTimeInSeconds = 0,
        .currentTime = 0,
        .numberOfEmittedParticles = 0,
        .disabled = false,
    };

    std::vector<Emitter3D> emits{};
    for(auto i = 0; i < globals.cpu->numSphereEmitters; ++i){
        Emitter3D emitter = prototype;
//        emitter.origin.x = domain.upper.x - 2.f * (radius + radius * static_cast<float>(i));
        emits.push_back(emitter);
    }
    emitters.sphere = device.createDeviceLocalBuffer(emits.data(), BYTE_SIZE(emits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void Collision3D::runSimulation(VkCommandBuffer commandBuffer) {
    if(pauseSim) return;

    Barrier::fragmentReadToComputeWrite(commandBuffer);
    emitParticles(commandBuffer);
    integrate(commandBuffer);
    solveConstraints(commandBuffer);
    updateVelocity(commandBuffer);
    checkBounds(commandBuffer);
    Barrier::computeWriteToFragmentRead(commandBuffer);

//    Barrier::computeWriteToTransferRead(commandBuffer);
//    device.copy(objects.position[1], objects.position[0], objects.position[0].size);
//    Barrier::transferWriteToComputeRead(commandBuffer);
}

void Collision3D::emitParticles(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = emitterDescriptorSet;

//    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.pipeline.handle);
//    vkCmdDispatch(commandBuffer, 1, 1, 1);
//    Barrier::computeWriteToRead(commandBuffer);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sphereEmitter.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sphereEmitter.pipeline.handle);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::integrate(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.pipeline.handle);
//    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::solveConstraints(VkCommandBuffer commandBuffer) {
    for(auto& constraint : compute.constraints) {
        solveConstraint(constraint, commandBuffer);
        applyCorrection(commandBuffer);
    }
}

void Collision3D::solveConstraint(Pipeline &pipeline, VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::applyCorrection(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.correction.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.correction.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::updateVelocity(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.velocity.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.velocity.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

void Collision3D::checkBounds(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.pipeline.handle);
    vkCmdDispatch(commandBuffer, globals.cpu->numObjects, 1, 1);
    Barrier::computeWriteToRead(commandBuffer);
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = Collision3D{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}