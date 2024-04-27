#define SQRT2 1.4142135623730950488016887242097
#include "Collision2d.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include <random>
#include <fmt/color.h>

Collision2d::Collision2d(const Settings& settings) : VulkanBaseApp("Collision Detection", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/collision2d");
    fileManager.addSearchPathFront("../../examples/collision2d/data");
    fileManager.addSearchPathFront("../../examples/collision2d/spv");
    fileManager.addSearchPathFront("../../examples/collision2d/models");
    fileManager.addSearchPathFront("../../examples/collision2d/textures");
}

void Collision2d::initApp() {
    initScratchBuffer();
    initObjects();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    initGrid();
    initSort();
    runDebug();
}

void Collision2d::initScratchBuffer() {
    scratchPad.buffer = device.createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_GPU_TO_CPU, (20 * (1 << 20)), "scratch_buffer");

}

void Collision2d::initObjects() {
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = glm::vec2(0);
    globals.cpu->domain.upper = glm::vec2(20);
    globals.cpu->gravity = 9.8;
    globals.cpu->numObjects = 50;
    globals.cpu->segmentSize = 2;

    std::default_random_engine engine{1 << 22};
    std::uniform_real_distribution<float> rDist{0.5, 1.0};
    std::uniform_real_distribution<float> xDist{0.5, 19.5};
    std::uniform_real_distribution<float> yDist{0.5, 19.5};

    auto nextPosition = [&]{ return glm::vec2{ xDist(engine), yDist(engine) }; };
    auto nextRadius = [&]{ return rDist(engine); };

    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocity(globals.cpu->numObjects);
    std::vector<float> radius{};
    std::vector<glm::vec4> colors(globals.cpu->numObjects, glm::vec4(1));
    float maxRadius = MIN_FLOAT;
    for(int i = 0; i < globals.cpu->numObjects; ++i){
        auto pos = nextPosition();
        auto r = nextRadius();
        maxRadius = glm::max(r, maxRadius);
        positions.push_back(pos);
        radius.push_back(r);
    }

    globals.cpu->halfSpacing = SQRT2 * maxRadius;
    globals.cpu->spacing = globals.cpu->halfSpacing * 2;
    glm::uvec2 dim{((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) + 1.f };
    auto numCells = dim.x * dim.y;

    objects.position = device.createCpuVisibleBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.velocity = device.createCpuVisibleBuffer(velocity.data(), BYTE_SIZE(velocity), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.radius = device.createCpuVisibleBuffer(radius.data(), BYTE_SIZE(radius), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.colors = device.createCpuVisibleBuffer(colors.data(), BYTE_SIZE(colors), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * globals.cpu->numObjects * 4);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * globals.cpu->numObjects * 4);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(Attribute) * globals.cpu->numObjects * 4);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(Attribute) * globals.cpu->numObjects * 4);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * (numCells + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(CellInfo) * numCells);
    objects.cellIndexStaging = reserve(sizeof(CellInfo) * numCells);
    objects.bitSet = reserve(sizeof(uint32_t) * numCells);
    objects.compactIndices = reserve(sizeof(uint32_t) * (numCells + 1));


    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, Dispatch::Size, "dispatch_cmd_buffer"); ;

    const auto& domain = globals.cpu->domain;
    globals.cpu->projection = glm::ortho(domain.lower.x, domain.upper.x, domain.lower.y, domain.upper.y);
    spdlog::info("spacing: {}, num cells: {}", globals.cpu->spacing, numCells);
    for(int i = 0; i < globals.cpu->numObjects; i++) {
        spdlog::info("obj[{}] => {} => {}", i, positions[i], glm::floor(positions[i]/globals.cpu->spacing));
    }
}

void Collision2d::initGrid() {
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

void Collision2d::initSort() {
    sort = RadixSort{ &device };
    sort.init();
    prefixSum = PrefixSum{ &device };
    prefixSum.init();
}

void Collision2d::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 5> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void Collision2d::createDescriptorSetLayouts() {
    objects.setLayout =
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
            .binding(7)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8)
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

    stagingSetLayout =
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
            .createLayout();
}
void Collision2d::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({globalSetLayout, objects.setLayout, stagingSetLayout});
    globalSet = sets[0];
    objects.descriptorSet = sets[1];
    stagingDescriptorSet = sets[2];

    auto writes = initializers::writeDescriptorSets<13>();

    writes[0].dstSet = globalSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;

    writes[1].dstSet = objects.descriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo positionInfo{ objects.position, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &positionInfo;

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
    VkDescriptorBufferInfo radiusInfo{ objects.radius, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &radiusInfo;

    writes[4].dstSet = objects.descriptorSet;
    writes[4].dstBinding = 3;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo colorInfo{ objects.colors, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &colorInfo;

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

    writes[10].dstSet = stagingDescriptorSet;
    writes[10].dstBinding = 0;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexStagingInfo{ *objects.cellIndexStaging.buffer, objects.cellIndexStaging.offset, objects.cellIndexStaging.end };
    writes[10].pBufferInfo = &cellIndexStagingInfo;

    writes[11].dstSet = stagingDescriptorSet;
    writes[11].dstBinding = 1;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo SetBitsInfo{*objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.end };
    writes[11].pBufferInfo = &SetBitsInfo;

    writes[12].dstSet = stagingDescriptorSet;
    writes[12].dstBinding = 2;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo compactInfo{ *objects.compactIndices.buffer, objects.compactIndices.offset, objects.compactIndices.end };
    writes[12].pBufferInfo = &compactInfo;


    device.updateDescriptorSets(writes);
}

void Collision2d::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Collision2d::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void Collision2d::createRenderPipeline() {
    //    @formatter:off
    auto builder = prototypes->cloneGraphicsPipeline();
    render.object.pipeline =
        builder
            .vertexInputState().clear()
            .inputAssemblyState()
                .points()
            .shaderStage()
                .vertexShader(resource("object.vert.spv"))
                .geometryShader(resource("object.geom.spv"))
                .fragmentShader(resource("object.frag.spv"))
            .rasterizationState()
                .cullNone()
            .depthStencilState()
                .compareOpAlways()
            .layout().clear()
                .addDescriptorSetLayout(globalSetLayout)
                .addDescriptorSetLayout(objects.setLayout)
            .name("render_object")
            .build(render.object.layout);

    render.objectCenter.pipeline =
        builder
            .shaderStage()
                .geometryShader(resource("object_center.geom.spv"))
            .name("render_object_center")
            .build(render.object.layout);

    render.objectBoundingBox.pipeline =
        builder
            .shaderStage()
                .geometryShader(resource("object_bounding_box.geom.spv"))
            .name("render_object_center")
            .build(render.objectBoundingBox.layout);
    //    @formatter:on
}

void Collision2d::createComputePipeline() {
    auto module = device.createShaderModule(resource("initialize_cell_ids.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.initCellIDs.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.initCellIDs.layout.handle;

    compute.initCellIDs.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("initialize_cellIDs", compute.initCellIDs.pipeline.handle);

    module = device.createShaderModule(resource("count_cells.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.countCells.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.countCells.layout.handle;
    compute.countCells.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("count_cells", compute.countCells.pipeline.handle);

    module = device.createShaderModule(resource("generate_cell_index_array.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.generateCellIndexArray.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout} );
    computeCreateInfo.layout = compute.generateCellIndexArray.layout.handle;
    compute.generateCellIndexArray.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_cell_index_array", compute.generateCellIndexArray.pipeline.handle);

    module = device.createShaderModule(resource("compact_cell_index_array.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.compactCellIndexArray.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout} );
    computeCreateInfo.layout = compute.compactCellIndexArray.layout.handle;
    compute.compactCellIndexArray.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compact_cell_index_array", compute.compactCellIndexArray.pipeline.handle);

    module = device.createShaderModule(resource("collision_test.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.collisionTest.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout}, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)} } );
    computeCreateInfo.layout = compute.collisionTest.layout.handle;
    compute.collisionTest.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("collision_test", compute.collisionTest.pipeline.handle);

    module = device.createShaderModule(resource("compute_dispatch.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.computeDispatch.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout}, { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2 }} );
    computeCreateInfo.layout = compute.computeDispatch.layout.handle;
    compute.computeDispatch.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_dispatch", compute.computeDispatch.pipeline.handle);

}


void Collision2d::onSwapChainDispose() {
    dispose(render.object.pipeline);
    dispose(compute.initCellIDs.pipeline);
    dispose(compute.countCells.pipeline);
}

void Collision2d::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *Collision2d::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    grid.canvas.draw(commandBuffer);
    renderObjects(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);


    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Collision2d::collisionDetection(VkCommandBuffer commandBuffer) {
    computeDispatch(commandBuffer, Dispatch::Object);
    initializeCellIds(commandBuffer);
    sortCellIds(commandBuffer);
    computeDispatch(commandBuffer, Dispatch::CellID);
    countCells(commandBuffer);
    generateCellIndexArray(commandBuffer);
    compactCellIndexArray(commandBuffer);
    computeDispatch(commandBuffer, Dispatch::CellArrayIndex);
}

void Collision2d::initializeCellIds(VkCommandBuffer commandBuffer) {
    globals.cpu->numCells = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::Object);

    VkBufferCopy region{0, 0, objects.cellIds.size};
    addComputeToTransferReadBarrier(commandBuffer,  { objects.cellIds, objects.attributes });
    vkCmdCopyBuffer(commandBuffer, objects.cellIds, prevCellIds, 1, &region);
    region.size = objects.attributes.size;
    vkCmdCopyBuffer(commandBuffer, objects.attributes, prevAttributes, 1, &region);

    addComputeBarrier(commandBuffer, { objects.attributes, objects.cellIds, globals.gpu });
}

void Collision2d::sortCellIds(VkCommandBuffer commandBuffer) {
    Records records{objects.attributes, 8};
    sort(commandBuffer, objects.cellIds, records);
    addComputeBarrier(commandBuffer, { objects.attributes, objects.cellIds });
}

void Collision2d::countCells(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;


    addComputeToTransferBarrier(commandBuffer, { objects.counts });
    vkCmdFillBuffer(commandBuffer, objects.counts, 0, objects.counts.size, 0);
    addTransferToComputeBarrier(commandBuffer, { objects.counts });

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellIDCmd);

    addComputeBarrier(commandBuffer, { objects.counts });
    prefixSum(commandBuffer, objects.counts);
    addComputeBarrier(commandBuffer, { objects.counts });
}

void Collision2d::generateCellIndexArray(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellIDCmd);
    addComputeBarrier(commandBuffer, { *objects.cellIndexStaging.buffer, *objects.bitSet.buffer });

    addComputeToTransferReadBarrier(commandBuffer, { *objects.bitSet.buffer  });
    VkBufferCopy region{objects.bitSet.offset, objects.compactIndices.offset, objects.bitSet.size() };
    vkCmdCopyBuffer(commandBuffer, *objects.bitSet.buffer, *objects.compactIndices.buffer, 1, &region);
    addTransferToComputeBarrier(commandBuffer, { *objects.compactIndices.buffer });

}

void Collision2d::compactCellIndexArray(VkCommandBuffer commandBuffer) {

    prefixSum(commandBuffer, objects.compactIndices);
    addComputeBarrier(commandBuffer, { *objects.compactIndices.buffer });

    globals.cpu->numCellIndices = 0;
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellIDCmd);
    addComputeBarrier(commandBuffer, { globals.gpu, objects.cellIndexArray });
}

void Collision2d::computeDispatch(VkCommandBuffer commandBuffer, uint32_t objectType) {
    static std::array<uint32_t, 2> constants{};
    constants[0] = 256;
    constants[1] = objectType;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.computeDispatch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.computeDispatch.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.computeDispatch.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, BYTE_SIZE(constants), constants.data());
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    addDispatchBarrier(commandBuffer, { objects.dispatchBuffer });
}

void Collision2d::renderObjects(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.object.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.object.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 1, globals.cpu->numObjects, 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.objectCenter.pipeline.handle);
    vkCmdDraw(commandBuffer, 1, globals.cpu->numObjects, 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.objectBoundingBox.pipeline.handle);
    vkCmdDraw(commandBuffer, 1, globals.cpu->numObjects, 0, 0);

}

void Collision2d::update(float time) {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        collisionDetection(commandBuffer);
    });
}

void Collision2d::checkAppInputs() {
    if(mouse.left.released) {
        Camera camera{.proj = globals.cpu->projection};
        auto pos = mousePositionToWorldSpace(camera);
//        spdlog::info("mouse pos: {}", pos);

        auto hash = [&](glm::vec2 pos) {
            glm::uvec2 dim{((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) + 1.f };
            glm::uvec2 pid{ pos/globals.cpu->spacing };
            return pid.y * dim.x + pid.x;
        };

        auto cellInfos = objects.cellIndexArray.span<CellInfo>(globals.cpu->numCellIndices);
        auto cellIds = objects.cellIds.span<uint32_t>();
        auto positions = objects.position.span<glm::vec2>();
        auto attributes = objects.attributes.span<Attribute>();

        auto cellHash = hash(pos.xy());
        std::string str{fmt::format("\nclicked position: {}\nCell Info\n\tID: {}\n", pos.xy(), cellHash) };
        for(auto info : cellInfos){
            auto begin = info.index;
            auto end = begin + info.numCells;

            if(cellIds[begin] == cellHash) {
                str += fmt::format("\toffset: {}\n\t{} home cells\n\t{} phantom cells\n", begin, info.numHomeCells, info.numPhantomCells);
            }
            for(auto i = begin; i < end; ++i ) {
                auto cell = cellIds[i];
                if(cell == cellHash){
                    str += fmt::format("\tobj: {}, ctrlBits: {:06b}\n", positions[attributes[i].objectID], attributes[i].controlBits);
                }
            }
        }
        spdlog::info("{}", str);
    }
}

void Collision2d::cleanup() {
    VulkanBaseApp::cleanup();
}

void Collision2d::onPause() {
    VulkanBaseApp::onPause();
}

void Collision2d::addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void Collision2d::addDispatchBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT);
}

void Collision2d::addComputeToTransferBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

void Collision2d::addComputeToTransferReadBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT);
}

void Collision2d::addTransferToComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_TRANSFER_BIT
            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

BufferRegion Collision2d::reserve(VkDeviceSize size) {
    size = alignedSize(size, device.getLimits().minStorageBufferOffsetAlignment);
    assert(scratchPad.offset + size <= scratchPad.buffer.size);
    auto start = scratchPad.offset;
    scratchPad.offset += size;
    return { &scratchPad.buffer, start, scratchPad.offset };
}

void Collision2d::runDebug() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        collisionDetection(commandBuffer);
    });

    glm::uvec2 dim{glm::ceil((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) };
    const auto numObjects = globals.cpu->numObjects;
    auto cellIds = prevCellIds.span<uint32_t>(numObjects * 4);
    auto attributes = prevAttributes.span<Attribute>(numObjects * 4);
    spdlog::info("num cells: {}", globals.cpu->numCells);

    const auto N = globals.cpu->numObjects;
    for(int i = 0; i < N; i++){
        spdlog::info("i: {}, id: {}, cells: [{}, {}, {}, {}], controlBits : {:06b} => {}", i, attributes[i].objectID, cellIds[i], cellIds[i + N], cellIds[i+ N *2], cellIds[i+ N * 3], attributes[i].controlBits, attributes[i].controlBits);
    }
    spdlog::info("");
    spdlog::info("Initial Cell ID Array");
    for(int i = 0; i < 4; ++i){
        std::string row{};
        for(int j = 0; j < globals.cpu->numObjects; ++j){
            int idx = j + globals.cpu->numObjects * i;
            if(cellIds[idx] == 0xFFFFFFFF){
                row += fmt::format("[     ] ");
            }else {
                auto cell = cellIds[idx];
                uint32_t cellType = ((cell % dim.x) % 2) + ((cell/dim.x) % 2) * 2;
                uint32_t homeCellType = attributes[j].controlBits & 0x3;
                if(cellType == homeCellType){
                    auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{:02}]", cellIds[idx], attributes[j].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{} ", label);
                }else {
                    auto label = fmt::format(bg(fmt::color::green), "[{:02}|{:02}]", cellIds[idx], attributes[j].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{} ", label);
                }
            }
        }
        spdlog::info("{}", row);
    }
    prevCellIds.unmap();
    prevAttributes.unmap();

    cellIds = objects.cellIds.span<uint32_t>(numObjects * 4);
    attributes = objects.attributes.span<Attribute>(numObjects * 4);
    spdlog::info("");
    spdlog::info("Sorted Cell ID Array");
    for(int i = 0; i < 4; ++i){
        std::string row{};
        for(int j = 0; j < globals.cpu->numObjects; ++j){
            int idx = j * 4 + i;
            if(cellIds[idx] == 0xFFFFFFFF){
                row += fmt::format("[     ] ");
            }else {
                auto cell = cellIds[idx];
                uint32_t cellType = ((cell % dim.x) % 2) + ((cell/dim.x) % 2) * 2;
                uint32_t homeCellType = attributes[idx].controlBits & 0x3;
                if(cellType == homeCellType){
                    auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{:02}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{} ", label);
                }else {
                    auto label = fmt::format(bg(fmt::color::green), "[{:02}|{:02}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{} ", label);
                }
            }
        }
        spdlog::info("{}", row);
    }

    std::string row{};


    std::span<CellInfo> cellInfos = { reinterpret_cast<CellInfo*>(objects.cellIndexArray.map()), static_cast<size_t>(globals.cpu->numCellIndices) };

    row = "";
    spdlog::info("");
    spdlog::info("Cell Index Array");
    for(auto cell : cellInfos) {
        std::string label = fmt::format(bg(fmt::color::cyan), "[{:02}|{}+{}]", cell.index, cell.numHomeCells, cell.numPhantomCells);
        label = fmt::format(fg(fmt::color::black), "{}", label);
        row += fmt::format("{} ", label);
    }
    spdlog::info(row);
    objects.cellIndexArray.unmap();


    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = globalSet;
        sets[1] = objects.descriptorSet;

        for(uint32_t pass = 0; pass < 4; ++pass) {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.layout.handle,0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.pipeline.handle);
            vkCmdPushConstants(commandBuffer, compute.collisionTest.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &pass);
            vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellArrayIndexCmd);
            addComputeBarrier(commandBuffer, { objects.colors });
        }
    });
}

int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enabledFeatures.geometryShader = true;
        settings.enableBindlessDescriptors = false;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = Collision2d{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}