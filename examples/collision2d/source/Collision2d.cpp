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
    globals.cpu->numCells = 0;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = globalSet;
        sets[1] = objects.descriptorSet;

        int gx = std::max(1, int((globals.cpu->numObjects)/256));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.initCellIDs.pipeline.handle);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    });

    glm::uvec2 dim{glm::ceil((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) };
    auto cellIds = reinterpret_cast<uint32_t*>(objects.cellIds.map());
    auto attributes = reinterpret_cast<Attribute*>(objects.attributes.map());
    spdlog::info("num cells: {}", globals.cpu->numCells);
    for(int i = 0; i < objects.cellIds.sizeAs<int>(); i+= 4){
        spdlog::info("id: {}, cells: [{}, {}, {}, {}], controlBits : {:06b}", attributes[i].objectID, cellIds[i], cellIds[i+1], cellIds[i+2], cellIds[i+3], attributes[i].controlBits);
    }
    spdlog::info("");
    for(int i = 0; i < 4; ++i){
        std::string row{};
        for(int j = 0; j < globals.cpu->numObjects; ++j){
            int idx = j * 4 + i;
            if(cellIds[idx] == 0xFFFFFFFF){
                row += fmt::format("[    ]\t");
            }else {
                auto cell = cellIds[idx];
                uint32_t cellType = ((cell % dim.x) % 2) + ((cell/dim.x) % 2) * 2;
                uint32_t homeCellType = attributes[idx].controlBits & 0x3;
                if(cellType == homeCellType){
                    auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{}\t", label);
                }else {
                    auto label = fmt::format(bg(fmt::color::green), "[{:02}|{}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{}\t", label);
                }
            }
        }
        spdlog::info("{}", row);
    }
    objects.cellIds.unmap();
    objects.attributes.unmap();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        Records records{objects.attributes, 8};
       sort(commandBuffer, objects.cellIds, records);
    });

    cellIds = reinterpret_cast<uint32_t*>(objects.cellIds.map());
    attributes = reinterpret_cast<Attribute*>(objects.attributes.map());
    spdlog::info("");
    for(int i = 0; i < 4; ++i){
        std::string row{};
        for(int j = 0; j < globals.cpu->numObjects; ++j){
            int idx = j * 4 + i;
            if(cellIds[idx] == 0xFFFFFFFF){
                row += fmt::format("[    ]\t");
            }else {
                auto cell = cellIds[idx];
                uint32_t cellType = ((cell % dim.x) % 2) + ((cell/dim.x) % 2) * 2;
                uint32_t homeCellType = attributes[idx].controlBits & 0x3;
                if(cellType == homeCellType){
                    auto label = fmt::format(bg(fmt::color::yellow), "[{:02}|{}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{}\t", label);
                }else {
                    auto label = fmt::format(bg(fmt::color::green), "[{:02}|{}]", cellIds[idx], attributes[idx].objectID);
                    label = fmt::format(fg(fmt::color::black), "{}", label);
                    row += fmt::format("{}\t", label);
                }
            }
        }
        spdlog::info("{}", row);
    }

    std::string row{};
    spdlog::info("");
    std::vector<uint32_t> cellsWithData{};
    std::set<uint32_t> processed{};
    for(int i = 0; i < globals.cpu->numCells; i++){
        if(!processed.contains(cellIds[i])) {
            cellsWithData.push_back(cellIds[i]);
        }
        processed.insert(cellIds[i]);
        row += fmt::format("{} ", cellIds[i]);
    }
    spdlog::info("{}", row);
    objects.cellIds.unmap();
    objects.attributes.unmap();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = globalSet;
        sets[1] = objects.descriptorSet;

        int gx = std::max(1, int((globals.cpu->numObjects)/256));

        addComputeToTransferBarrier(commandBuffer, { objects.counts });
        vkCmdFillBuffer(commandBuffer, objects.counts, 0, objects.counts.size, 0);
        addTransferToComputeBarrier(commandBuffer, { objects.counts });

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.countCells.pipeline.handle);
        vkCmdDispatch(commandBuffer, gx, 1, 1);
    });

    std::span<uint32_t> counts = { reinterpret_cast<uint32_t*>(objects.counts.map()), size_t{objects.counts.sizeAs<uint32_t>() } };
    spdlog::info("");
    row = "";
    for(auto i : cellsWithData) {
        row += fmt::format("{}:{} ", i, counts[i]);
    }
    spdlog::info(row);
    objects.counts.unmap();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
       prefixSum(commandBuffer, objects.counts);
    });

    counts = { reinterpret_cast<uint32_t*>(objects.counts.map()), size_t{objects.counts.sizeAs<uint32_t>() } };
    spdlog::info("");
    row = "";
    for(int i = 0; i < counts.size(); i++){
        row += fmt::format("{}:{} ", i, counts[i]);
    }
    spdlog::info(row);
}

void Collision2d::initObjects() {
    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(GlobalData));
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());

    globals.cpu->domain.lower = glm::vec2(0);
    globals.cpu->domain.upper = glm::vec2(20);
    globals.cpu->gravity = 9.8;
    globals.cpu->numObjects = 20;
    globals.cpu->segmentSize = 100;

    std::default_random_engine engine{1 << 22};
    std::uniform_real_distribution<float> rDist{0.5, 1.0};
    std::uniform_real_distribution<float> xDist{0.5, 19.5};
    std::uniform_real_distribution<float> yDist{0.5, 19.5};

    auto nextPosition = [&]{ return glm::vec2{ xDist(engine), yDist(engine) }; };
    auto nextRadius = [&]{ return rDist(engine); };

    std::vector<glm::vec2> positions;
    std::vector<glm::vec2> velocity(globals.cpu->numObjects);
    std::vector<float> radius{};
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
    glm::uvec2 dim{glm::ceil((globals.cpu->domain.upper - globals.cpu->domain.lower)/globals.cpu->spacing) };
    auto numCells = dim.x * dim.y;

    objects.position = device.createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.velocity = device.createDeviceLocalBuffer(velocity.data(), BYTE_SIZE(velocity), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.radius = device.createDeviceLocalBuffer(radius.data(), BYTE_SIZE(radius), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    objects.cellIds = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * globals.cpu->numObjects * 4);
    objects.attributes = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(Attribute) * globals.cpu->numObjects * 4);
    objects.counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * numCells);
    objects.offsets = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(uint32_t) * numCells);

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
    prefixSum.updateDataDescriptorSets(objects.counts);
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
            .createLayout();

    globalSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}
void Collision2d::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({globalSetLayout, objects.setLayout});
    globalSet = sets[0];
    objects.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<8>();

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
    VkDescriptorBufferInfo cellIdInfo{ objects.cellIds, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &cellIdInfo;

    writes[5].dstSet = objects.descriptorSet;
    writes[5].dstBinding = 4;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo attributesInfo{ objects.attributes, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &attributesInfo;

    writes[6].dstSet = objects.descriptorSet;
    writes[6].dstBinding = 5;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ objects.counts, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &countsInfo;

    writes[7].dstSet = objects.descriptorSet;
    writes[7].dstBinding = 6;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    VkDescriptorBufferInfo offsetsInfo{ objects.counts, 0, VK_WHOLE_SIZE };
    writes[7].pBufferInfo = &offsetsInfo;

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

    module = device.createShaderModule(resource("count_cells.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    compute.countCells.layout = device.createPipelineLayout( { globalSetLayout, objects.setLayout} );
    computeCreateInfo.layout = compute.countCells.layout.handle;
    compute.countCells.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

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
}

void Collision2d::checkAppInputs() {
}

void Collision2d::cleanup() {
    VulkanBaseApp::cleanup();
}

void Collision2d::onPause() {
    VulkanBaseApp::onPause();
}

void Collision2d::addComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
}

void Collision2d::addComputeToTransferBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
            , VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
}

void Collision2d::addTransferToComputeBarrier(VkCommandBuffer commandBuffer, const std::vector<VulkanBuffer> &buffers) {
    addBufferMemoryBarriers(commandBuffer, buffers, VK_PIPELINE_STAGE_TRANSFER_BIT
            , VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
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