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
    initEmitters();
    initObjectsForDebugging();
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

void Collision2d::initEmitters() {
    if(debugMode) {
        emitters = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, 4);
        return;
    }
    const auto& domain = globals.cpu->domain;
    const auto radius = objects.defaultRadius;
    const auto diameter = radius * 2;
    globals.cpu->numEmitters = 10;

    Emitter prototype{
        .origin = { domain.upper.x - diameter, domain.upper.y - diameter },
        .direction = {-1, 0},
        .radius = radius,
        .offset = 1.25,
        .speed = 10,
        .spreadAngleRad = 0,
        .maxNumberOfParticlePerSecond = 100,
        .maxNumberOfParticles = static_cast<int>(glm::max(1u, objects.maxParticles / globals.cpu->numEmitters)),
        .firstFrameTimeInSeconds = 0,
        .currentTime = 0,
        .numberOfEmittedParticles = 0,
        .disabled = false,
    };

    std::vector<Emitter> emits{};
    for(auto i = 0; i < globals.cpu->numEmitters; ++i){
        Emitter emitter = prototype;
        emitter.origin.y = domain.upper.y - 2.f * (radius + radius * static_cast<float>(i));
        emits.push_back(emitter);
    }
    emitters = device.createDeviceLocalBuffer(emits.data(), BYTE_SIZE(emits), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void Collision2d::initScratchBuffer() {
    scratchPad.buffer = device.createBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_GPU_TO_CPU, (20 * (1 << 20)), "scratch_buffer");

}

void Collision2d::initObjects() {
    if(debugMode) return;
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = glm::vec2(0);
    globals.cpu->domain.upper = glm::vec2(20);
    globals.cpu->gravity = 9.8;
    globals.cpu->numObjects = 0;
    globals.cpu->segmentSize = 2;
    globals.cpu->frame = 0;
    globals.cpu->time = fixedUpdate.period();
    globals.cpu->numObjects = 0;

    globals.cpu->halfSpacing = objects.defaultRadius;
    globals.cpu->spacing = SQRT2 * objects.defaultRadius * 3;
    glm::uvec2 dim{((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) + 1.f };
    globals.cpu->gridSize = objects.gridSize = dim.x * dim.y;


    uint32_t numParticle = objects.maxParticles;

    std::vector<glm::vec4> colors(numParticle, glm::vec4(1, 0, 0, 1) );
    objects.position = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec2));
    objects.velocity = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(glm::vec2));
    objects.radius = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, numParticle * sizeof(float));
    objects.colors = device.createDeviceLocalBuffer(colors.data(), BYTE_SIZE(colors), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle * 4);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(uint32_t) * numParticle* 4);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 4);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(Attribute) * numParticle * 4);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, memoryUsage, sizeof(uint32_t) * (objects.gridSize + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, sizeof(CellInfo) * objects.gridSize);
    objects.cellIndexStaging = reserve(sizeof(CellInfo) * objects.gridSize);
    objects.bitSet = reserve(sizeof(uint32_t) * std::max(objects.gridSize, numParticle));
    objects.compactIndices = reserve(sizeof(uint32_t) * (objects.gridSize + 1));
    objects.indices = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, memoryUsage, numParticle * sizeof(uint32_t) * 4);


    updatesBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, memoryUsage, sizeof(UpdateInfo) * numParticle * 10);
    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, memoryUsage, Dispatch::Size, "dispatch_cmd_buffer"); ;

    const auto& domain = globals.cpu->domain;
//    globals.cpu->projection = glm::ortho(domain.lower.x, domain.upper.x, domain.lower.y, domain.upper.y);
    globals.cpu->projection = vkn::ortho(domain.lower.x, domain.upper.x, domain.lower.y, domain.upper.y);
}


void Collision2d::initObjectsForDebugging() {
    if(!debugMode) return;
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = glm::vec2(0);
    globals.cpu->domain.upper = glm::vec2(20);
    globals.cpu->gravity = 9.8;
    globals.cpu->numObjects = 20;
    globals.cpu->segmentSize = 2;
    globals.cpu->frame = 0;
    globals.cpu->time = 0;

    std::default_random_engine engine{1 << 22};
    std::uniform_real_distribution<float> rDist{0.5, 1.0};
    std::uniform_real_distribution<float> xDist{0.5, 19.5};
    std::uniform_real_distribution<float> yDist{0.5, 19.5};

    auto nextPosition = [&]{ return glm::vec2{ xDist(engine), yDist(engine) }; };
    auto nextRadius = [&]{ return rDist(engine); };

    float maxRadius = MIN_FLOAT;
    std::vector<glm::vec2> positions{};
    std::vector<glm::vec2> velocity(globals.cpu->numObjects);
    std::vector<float> radius{};
    std::vector<glm::vec4> colors(globals.cpu->numObjects, glm::vec4(1));


    for(int i = 0; i < globals.cpu->numObjects; ++i){
        auto pos = nextPosition();
        auto r = nextRadius();
        maxRadius = glm::max(r, maxRadius);
        positions.push_back(pos);
        radius.push_back(r);
    }

//    globals.cpu->numObjects = 3;
//    float maxRadius = objects.defaultRadius;
//    std::vector<glm::vec2> positions(globals.cpu->numObjects);
//    std::vector<glm::vec2> velocity(globals.cpu->numObjects);
//    std::vector<float> radius(globals.cpu->numObjects, maxRadius);
//    std::vector<glm::vec4> colors(globals.cpu->numObjects, glm::vec4(1));
//
//    positions[0] = {4.3559451103, 0.9999574423};
//    positions[1] = {6.7158102989, 0.9999574423};
//    positions[2] = {2.8033828735, 0.9999574423};

    globals.cpu->halfSpacing = SQRT2 * maxRadius;
    globals.cpu->spacing = globals.cpu->halfSpacing * 2;
    glm::uvec2 dim{((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) + 1.f };
    globals.cpu->gridSize = objects.gridSize = dim.x * dim.y;

    objects.position = device.createCpuVisibleBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.velocity = device.createCpuVisibleBuffer(velocity.data(), BYTE_SIZE(velocity), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.radius = device.createCpuVisibleBuffer(radius.data(), BYTE_SIZE(radius), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.colors = device.createCpuVisibleBuffer(colors.data(), BYTE_SIZE(colors), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * globals.cpu->numObjects * 4);
    prevCellIds = device.createBuffer( VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * globals.cpu->numObjects * 4);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(Attribute) * globals.cpu->numObjects * 4);
    prevAttributes = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(Attribute) * globals.cpu->numObjects * 4);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * (objects.gridSize  + 1));
    objects.cellIndexArray = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(CellInfo) * objects.gridSize );
    objects.cellIndexStaging = reserve(sizeof(CellInfo) * objects.gridSize );
    objects.bitSet = reserve(sizeof(uint32_t) * objects.gridSize );
    objects.compactIndices = reserve(sizeof(uint32_t) * (objects.gridSize  + 1));
    objects.indices = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, globals.cpu->numObjects * sizeof(uint32_t) * 4);


    updatesBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(UpdateInfo) * globals.cpu->numObjects * 10);
    objects.dispatchBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, Dispatch::Size, "dispatch_cmd_buffer"); ;

    const auto& domain = globals.cpu->domain;
    globals.cpu->projection = glm::ortho(domain.lower.x, domain.upper.x, domain.lower.y, domain.upper.y);
    spdlog::info("spacing: {}, num cells: {}", globals.cpu->spacing, objects.gridSize );
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
//    sort.enableOrderChecking();
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

    emitterSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}
void Collision2d::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({globalSetLayout, objects.setLayout, stagingSetLayout, emitterSetLayout});
    globalSet = sets[0];
    objects.descriptorSet = sets[1];
    stagingDescriptorSet = sets[2];
    emitterDescriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<16>();

    writes[0].dstSet = globalSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;


    writes[1].dstSet = globalSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo updatesInfo{ updatesBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &updatesInfo;

    writes[2].dstSet = objects.descriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo positionInfo{ objects.position, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &positionInfo;

    writes[3].dstSet = objects.descriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ objects.velocity, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &velocityInfo;

    writes[4].dstSet = objects.descriptorSet;
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo radiusInfo{ objects.radius, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &radiusInfo;

    writes[5].dstSet = objects.descriptorSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo colorInfo{ objects.colors, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &colorInfo;

    writes[6].dstSet = objects.descriptorSet;
    writes[6].dstBinding = 4;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo cellIdInfo{ objects.cellIds, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &cellIdInfo;

    writes[7].dstSet = objects.descriptorSet;
    writes[7].dstBinding = 5;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo attributesInfo{ objects.attributes, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &attributesInfo;

    writes[8].dstSet = objects.descriptorSet;
    writes[8].dstBinding = 6;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ objects.counts, 0, VK_WHOLE_SIZE };
    writes[8].pBufferInfo = &countsInfo;

    writes[9].dstSet = objects.descriptorSet;
    writes[9].dstBinding = 7;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexInfo{ objects.cellIndexArray, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &cellIndexInfo;

    writes[10].dstSet = objects.descriptorSet;
    writes[10].dstBinding = 8;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchCmdInfo{ objects.dispatchBuffer, 0, VK_WHOLE_SIZE };
    writes[10].pBufferInfo = &dispatchCmdInfo;

    writes[11].dstSet = stagingDescriptorSet;
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo cellIndexStagingInfo{ *objects.cellIndexStaging.buffer, objects.cellIndexStaging.offset, objects.cellIndexStaging.end };
    writes[11].pBufferInfo = &cellIndexStagingInfo;

    writes[12].dstSet = stagingDescriptorSet;
    writes[12].dstBinding = 1;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo SetBitsInfo{*objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.end };
    writes[12].pBufferInfo = &SetBitsInfo;

    writes[13].dstSet = stagingDescriptorSet;
    writes[13].dstBinding = 2;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[13].descriptorCount = 1;
    VkDescriptorBufferInfo compactInfo{ *objects.compactIndices.buffer, objects.compactIndices.offset, objects.compactIndices.end };
    writes[13].pBufferInfo = &compactInfo;


    writes[14].dstSet = emitterDescriptorSet;
    writes[14].dstBinding = 0;
    writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[14].descriptorCount = 1;
    VkDescriptorBufferInfo emitterInfo{ emitters, 0, VK_WHOLE_SIZE};
    writes[14].pBufferInfo = &emitterInfo;

    writes[15].dstSet = objects.descriptorSet;
    writes[15].dstBinding = 9;
    writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[15].descriptorCount = 1;
    VkDescriptorBufferInfo indicesInfo{ objects.indices, 0, VK_WHOLE_SIZE };
    writes[15].pBufferInfo = &indicesInfo;


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

    module = device.createShaderModule(resource(debugMode ? "collision_test.comp.spv" : "resolve_collision_verlet.comp.spv"));
//    module = device.createShaderModule(resource( "collision_test.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.collisionTest.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, stagingSetLayout}, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)} } );
    computeCreateInfo.layout = compute.collisionTest.layout.handle;
    compute.collisionTest.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("collision_test", compute.collisionTest.pipeline.handle);

    module = device.createShaderModule(resource("brute_force_resolve_collision.comp.spv"));
//    module = device.createShaderModule(resource( "collision_test.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.bruteForceTest.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout});
    computeCreateInfo.layout = compute.bruteForceTest.layout.handle;
    compute.bruteForceTest.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("brute_forcecollision_test", compute.bruteForceTest.pipeline.handle);

    module = device.createShaderModule(resource("compute_dispatch.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.computeDispatch.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout}, { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t) * 2 }} );
    computeCreateInfo.layout = compute.computeDispatch.layout.handle;
    compute.computeDispatch.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_dispatch", compute.computeDispatch.pipeline.handle);

    module = device.createShaderModule(resource("emitter_verlet.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.emitter.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout, emitterSetLayout} );
    computeCreateInfo.layout = compute.emitter.layout.handle;
    compute.emitter.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("emitter", compute.emitter.pipeline.handle);

    module = device.createShaderModule(resource("integrate_verlet.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.integrate.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.integrate.layout.handle;
    compute.integrate.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("integrate", compute.integrate.pipeline.handle);


    module = device.createShaderModule(resource("bounds_check_verlet.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.boundsCheck.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.boundsCheck.layout.handle;
    compute.boundsCheck.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("bounds_check", compute.boundsCheck.pipeline.handle);

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

    if(debugMode) {
        grid.canvas.draw(commandBuffer);
    }
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
    resolveCollision(commandBuffer);
}

void Collision2d::initializeCellIds(VkCommandBuffer commandBuffer) {
    globals.cpu->numCells = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdFillBuffer(commandBuffer, objects.cellIds, 0, objects.cellIds.size, 0xFFFFFFFF);
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
    sort.sortWithIndices(commandBuffer, objects.cellIds, objects.indices);
    addComputeBarrier(commandBuffer, { objects.cellIds, objects.indices });
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

    uint32_t gx = objects.gridSize/workGroupSize + glm::sign(static_cast<float>(objects.gridSize % workGroupSize));


    vkCmdFillBuffer(commandBuffer, *objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.size(), 0);
    vkCmdFillBuffer(commandBuffer, *objects.compactIndices.buffer, objects.compactIndices.offset, objects.compactIndices.size(), 0);
    vkCmdFillBuffer(commandBuffer, *objects.cellIndexStaging.buffer, objects.cellIndexStaging.offset, objects.cellIndexStaging.size(), 0);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.generateCellIndexArray.pipeline.handle);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { *objects.cellIndexStaging.buffer, *objects.bitSet.buffer });

    addComputeToTransferReadBarrier(commandBuffer, { *objects.bitSet.buffer  });
    VkBufferCopy region{objects.bitSet.offset, objects.compactIndices.offset, objects.bitSet.size() };
    vkCmdCopyBuffer(commandBuffer, *objects.bitSet.buffer, *objects.compactIndices.buffer, 1, &region);
    addTransferToComputeBarrier(commandBuffer, { *objects.compactIndices.buffer });

}

void Collision2d::compactCellIndexArray(VkCommandBuffer commandBuffer) {

    prefixSum(commandBuffer, objects.compactIndices);
    addComputeBarrier(commandBuffer, { *objects.compactIndices.buffer });

    uint32_t gx = objects.gridSize/workGroupSize + glm::sign(static_cast<float>(objects.gridSize % workGroupSize));
    globals.cpu->numCellIndices = 0;
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    vkCmdFillBuffer(commandBuffer, objects.cellIndexArray, 0, VK_WHOLE_SIZE, 0);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.compactCellIndexArray.pipeline.handle);
    vkCmdDispatch(commandBuffer, gx, 1, 1);
    addComputeBarrier(commandBuffer, { globals.gpu, objects.cellIndexArray });
}

void Collision2d::resolveCollision(VkCommandBuffer commandBuffer) {
//    static int wait = 0;
//    wait++;
//    if(wait < 240) return;
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = stagingDescriptorSet;

    vkCmdFillBuffer(commandBuffer, *objects.bitSet.buffer, objects.bitSet.offset, objects.bitSet.size(), 0);
    for(uint32_t pass = 0; pass < 4; ++pass) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.layout.handle,0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.collisionTest.pipeline.handle);
        vkCmdPushConstants(commandBuffer, compute.collisionTest.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t), &pass);
        vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::CellArrayIndexCmd);
        addComputeBarrier(commandBuffer, { objects.position, objects.velocity, *objects.bitSet.buffer });
    }
}

void Collision2d::bruteForceResolveCollision(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.bruteForceTest.layout.handle,0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.bruteForceTest.pipeline.handle);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    addComputeBarrier(commandBuffer, { objects.position, objects.velocity });
}

void Collision2d::emitParticles(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;
    sets[2] = emitterDescriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.emitter.pipeline.handle);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    addComputeBarrier(commandBuffer, { globals.gpu, objects.position, objects.radius });
}

void Collision2d::boundsCheck(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.boundsCheck.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    addComputeBarrier(commandBuffer, { globals.gpu, objects.position, objects.velocity });
}

void Collision2d::integrate(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = globalSet;
    sets[1] = objects.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.pipeline.handle);
    vkCmdDispatchIndirect(commandBuffer, objects.dispatchBuffer, Dispatch::ObjectCmd);
    addComputeBarrier(commandBuffer, { globals.gpu, objects.position, objects.velocity });
}

void Collision2d::computeDispatch(VkCommandBuffer commandBuffer, uint32_t objectType) {
    static std::array<uint32_t, 2> constants{};
    constants[0] = workGroupSize;
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

    if(debugMode) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.objectCenter.pipeline.handle);
        vkCmdDraw(commandBuffer, 1, globals.cpu->numObjects, 0, 0);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.objectBoundingBox.pipeline.handle);
        vkCmdDraw(commandBuffer, 1, globals.cpu->numObjects, 0, 0);
    }

}

void Collision2d::update(float time) {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        vkCmdFillBuffer(commandBuffer, objects.cellIds, 0, VK_WHOLE_SIZE, 0xFFFFFFFFu);
        Barrier::transferWriteToComputeRead(commandBuffer, objects.cellIds);
    });
    if(!debugMode) {
        fixedUpdate([&] {
            auto frameCount = fixedUpdate.frames();
            device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
                emitParticles(commandBuffer);
                computeDispatch(commandBuffer, Dispatch::Object);
                boundsCheck(commandBuffer);
                if(useBruteForce){
                    bruteForceResolveCollision(commandBuffer);
                }else {
                    collisionDetection(commandBuffer);
                }
                integrate(commandBuffer);
            });

            glfwSetWindowTitle(window, fmt::format("{} - FPS {}, frameCount {}, numObjects {}, numCells {}", title, framePerSecond, frameCount, globals.cpu->numObjects, globals.cpu->numCells).c_str());
        });
        fixedUpdate.advance(time);
        globals.cpu->frame++;
    }
    if(memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU) {
        auto positions = objects.position.span<glm::vec2>(globals.cpu->numObjects);
        auto velocity = objects.velocity.span<glm::vec2>(globals.cpu->numObjects);
        auto radius = objects.radius.span<float>(globals.cpu->numObjects);
        auto attributes = objects.attributes.span<Attribute>();
        auto offsets = objects.counts.span<uint>(12);
        auto cells = objects.cellIds.span<uint32_t>();
        auto cellIndexArray = objects.cellIndexArray.span<CellInfo>(globals.cpu->numCellIndices);
        auto dispatchCommands = objects.dispatchBuffer.span<glm::uvec4>(Dispatch::Count);
        auto indices = objects.indices.span<uint32_t>();
        auto &p = positions;
        auto &v = velocity;
    }


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

    if(mouse.right.released) {
//        useBruteForce = !useBruteForce;
//        if(useBruteForce){
//            spdlog::info("using brute force solver");
//        }
        fixedUpdate.paused() ? fixedUpdate.unPause() : fixedUpdate.pause();
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
    if(!debugMode) return;
    globals.cpu->numUpdates = 0;
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
    auto cellInfoBC = objects.cellIndexStaging.span<CellInfo>();
    auto bitSet = objects.bitSet.span<uint32_t>();
    auto compactIndices = objects.compactIndices.span<uint32_t>();
    auto dispatch = objects.dispatchBuffer.span<glm::uvec4>();
    auto offsets = objects.counts.span<uint32_t>();
    auto updates = updatesBuffer.span<UpdateInfo>(globals.cpu->numUpdates);
    auto positions = objects.position.span<glm::vec2>();

    spdlog::info("");
    for(int i = 0; i < globals.cpu->numUpdates; i += 2){
        auto a = updates[i];
        for(auto j = 0; j < globals.cpu->numUpdates; j++){
            if(i == j) continue;
            auto b = updates[j];

            if(a.tid != b.tid && a.objectId == b.objectId && a.pass == b.pass) {
                b = updates[j % 2 == 0 ? j + 1 : j - 1];
                glm::uvec2 gpa = glm::uvec2(glm::floor(positions[a.objectId]/globals.cpu->spacing));
                glm::uvec2 gpb = glm::uvec2(glm::floor(positions[b.objectId]/globals.cpu->spacing));
                spdlog::info("[{}, {}]: [[{}, {}] | {}] <=> [{}, {}] | {}  updated by threads {} & {} in pass {}, cellInfo: [{}, {}]"
                             ,a.objectId, b.objectId,gpa.x, gpa.y,  positions[a.objectId], gpb.x, gpb.y,  positions[b.objectId], a.tid, b.tid, a.pass, a.cellID, b.cellID);
            }
        }
    }
    spdlog::info("");
}

void Collision2d::endFrame() {
//    globals.cpu->frame++;
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