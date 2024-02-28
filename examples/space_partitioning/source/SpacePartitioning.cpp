#include "SpacePartitioning.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "kdtree.hpp"
#include "PoissonDiskSampling.hpp"

SpacePartitioning::SpacePartitioning(const Settings& settings)
: VulkanBaseApp("Space partitioning", settings)
{
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/space_partitioning");
    fileManager.addSearchPathFront("../../examples/space_partitioning/data");
    fileManager.addSearchPathFront("../../examples/space_partitioning/spv");
    fileManager.addSearchPathFront("../../examples/space_partitioning/models");
    fileManager.addSearchPathFront("../../examples/space_partitioning/textures");

    addSplit = &mapToKey(Key::D, "add split", Action::detectInitialPressOnly());
    removeSplit = &mapToKey(Key::A, "add split", Action::detectInitialPressOnly());
    clearSplits = &mapToKey(Key::C, "clear split", Action::detectInitialPressOnly());
    treeToggle = &mapToKey(Key::T, "Show/hide kd-tree", Action::detectInitialPressOnly());
}

void SpacePartitioning::initApp() {
    initQuery();
    createPoints();
    initCamera();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void SpacePartitioning::initQuery() {
    VkQueryPoolCreateInfo info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = 2;
    ERR_GUARD_VULKAN(vkCreateQueryPool(device, &info, nullptr, &queryPool));
}

void SpacePartitioning::createPoints() {
    pointBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Point) * (numPoints + 1));
    points = reinterpret_cast<Point*>(pointBuffer.map());
    searchResultBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, numPoints * sizeof(int), "search_results");
    (*reinterpret_cast<int*>(searchResultBuffer.map())) = -1;
    searchResultBuffer.unmap();
    generatePoints();
}

void SpacePartitioning::generatePoints() {
//    auto blueNoise = PoissonDiskSampling::generate({ glm::vec2{-1}, glm::vec2{1} }, 0.5);
//    spdlog::error("{} blue noise samples generated", blueNoise.size());
//    std::vector<Point> samples;
//
//    for(auto p : blueNoise) {
//        samples.push_back({ .position = p});
//    }
//    numPoints = samples.size();

    std::vector<Point> samples(numPoints);
    std::generate(samples.begin(), samples.end(), [&]{
        Point point{ .position = randomVec3(glm::vec3(-1), glm::vec3(1), glm::uvec3(1 << 20, 1 << 10, 1 << 15)).xy() };
        return point;
    });

    int count = 0;
    tree = kdtree::balance(samples, Bounds{glm::vec2(-1), glm::vec2(1) }, count);
    spdlog::info("{} kd-tree entries, tree size: {}", count, tree.size());
//    spdlog::info("kd-tree: {}", tree);


//    spdlog::info("\n");
//    for(auto sample : samples) {
//        spdlog::info("{}", sample.position);
//    }
//
//    spdlog::info("\n");
//    for(auto i = 0; i < tree.size(); i++){
//        spdlog::info("node index: {} point: {}", i,samples[tree[i]].position);
//    }

    treeBuffer = device.createDeviceLocalBuffer(tree.data(), BYTE_SIZE(tree), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    std::memcpy(points, samples.data(), sizeof(Point) * samples.size());

}

void SpacePartitioning::initCamera() {
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


void SpacePartitioning::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 13> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void SpacePartitioning::createDescriptorSetLayouts() {
    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("main_descriptor_setlayout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void SpacePartitioning::updateDescriptorSets(){
    descriptorSet = descriptorPool.allocate({ descriptorSetLayout }).front();

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("points_descriptor_set", descriptorSet);
    
    auto writes = initializers::writeDescriptorSets<3>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo pointInfo{ pointBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &pointInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo treeInfo{ treeBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &treeInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo searchInfo{ searchResultBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &searchInfo;

    device.updateDescriptorSets(writes);
}

void SpacePartitioning::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SpacePartitioning::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SpacePartitioning::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .allowDerivatives()
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .shaderStage()
                    .vertexShader(resource("point.vert.spv"))
                    .fragmentShader(resource("point.frag.spv"))
                .layout().clear()
                    .addDescriptorSetLayout(descriptorSetLayout)
                .name("point")
                .build(render.layout);

        splitAxis.pipeline =
            builder
                .basePipeline(render.pipeline)
                .shaderStage()
                    .geometryShader(resource("split_axis.geom.spv"))
                .rasterizationState()
                    .lineWidth(2)
                .name("split_axis")
                .build(splitAxis.layout);

        treeVisual.node.pipeline =
            builder
                .shaderStage()
                    .geometryShader(resource("node.geom.spv"))
                .name("kd_tree_node")
                .build(treeVisual.node.layout);

        treeVisual.edge.pipeline =
            builder
                .shaderStage()
                    .geometryShader(resource("edge.geom.spv"))
                .name("kd_tree_edge")
                .build(treeVisual.edge.layout);

        search.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("search_area.vert.spv"))
                    .geometryShader(resource("search_area.geom.spv"))
                .rasterizationState()
                    .lineWidth(2)
                .layout().clear()
                    .addPushConstantRange({VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SearchArea)})
                .name("search_area")
                .build(search.layout);
    //    @formatter:on
}

void SpacePartitioning::createComputePipeline() {
    auto module = device.createShaderModule( resource("kdtree_search.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    searchCompute.layout =
            device.createPipelineLayout(
                    { descriptorSetLayout},
                    {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(searchCompute.constants)} });

    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("search_compute_layout", searchCompute.layout.handle);

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = searchCompute.layout.handle;

    searchCompute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("search_compute", searchCompute.pipeline.handle);

}

void SpacePartitioning::onSwapChainDispose() {
    dispose(render.pipeline);
}

void SpacePartitioning::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *SpacePartitioning::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    renderPoints(commandBuffer);
    renderSpitAxis(commandBuffer);

    if(visualizeTree){
        renderKdTree(commandBuffer);
    }

    renderSearchArea(commandBuffer);

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SpacePartitioning::renderPoints(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, numPoints, 0, 0);
}

void SpacePartitioning::renderSpitAxis(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, splitAxis.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, splitAxis.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, numSplits, 0, 0);
}


void SpacePartitioning::renderKdTree(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, treeVisual.node.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, treeVisual.node.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, tree.size(), 0, 0);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, treeVisual.edge.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, treeVisual.edge.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, tree.size(), 0, 0);
}

void SpacePartitioning::renderSearchArea(VkCommandBuffer commandBuffer) {
    if(search.on){
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, search.pipeline.handle);
        vkCmdPushConstants(commandBuffer, search.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SearchArea), &searchArea);
        vkCmdDraw(commandBuffer, 1, 1, 0, 0);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDraw(commandBuffer, 1, numPoints + 1, 0, numPoints);
    }
}

void SpacePartitioning::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Space partitioning");
    ImGui::SetWindowSize({0, 0});

    static int context = 0;
    ImGui::RadioButton("Run on cpu", &context, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Run on gpu", &context, 1);
    useGPU = context == 1;
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void SpacePartitioning::update(float time) {

}

void SpacePartitioning::checkAppInputs() {
    if(addSplit->isPressed()){
        numSplits++;
    }
    if(removeSplit->isPressed()){
        numSplits--;
    }
    if(clearSplits->isPressed()){
        numSplits = 0;
    }
    numSplits = glm::clamp(numSplits, 0, numPoints);

    if(treeToggle->isPressed()){
        visualizeTree = !visualizeTree;
    }


    static bool initialPress = false;
    if(mouse.left.held) {
        if(!initialPress) {
            initialPress = true;
            searchArea.position = mousePositionToWorldSpace({}).xy();
            points[numPoints].position = searchArea.position;
            points[numPoints].color = {1, 1, 0, 1};
            search.on = true;
        }
        glm::vec2 position = mousePositionToWorldSpace({}).xy();
        searchArea.radius = glm::distance(searchArea.position, position);
    }

    if(initialPress && mouse.left.released) {
        initialPress = false;

        if(searchArea.radius != 0) {
            spdlog::info("search radius: {}", searchArea.radius);
            executeSearch = true;
        }
    }

    if(mouse.right.released){
        clearQueryPoints();
        search.on = false;
    }
}

void SpacePartitioning::cleanup() {
    VulkanBaseApp::cleanup();
}

void SpacePartitioning::onPause() {
    VulkanBaseApp::onPause();
}

void SpacePartitioning::endFrame() {
    if (executeSearch) {
        executeSearch = false;
        if (useGPU) {
            runGpuSearch();
        } else {
            runCpuSearch();
        }
    }
}

void SpacePartitioning::runGpuSearch() {
    executeSearch = false;
    clearQueryPoints();

    searchCompute.constants.position = searchArea.position;
    searchCompute.constants.radius = searchArea.radius;
    searchCompute.constants.numPoints = numSearchPoints;
    searchCompute.constants.treeSize = tree.size();



    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){

        vkCmdResetQueryPool(commandBuffer, queryPool, 0, 2);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 0);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, searchCompute.pipeline.handle);
        vkCmdPushConstants(commandBuffer, searchCompute.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(searchCompute.constants), &searchCompute.constants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, searchCompute.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queryPool, 1);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

    vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(uint64_t) *2
                          , &searchCompute.runtime, sizeof(uint64_t)
                          , VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    auto duration = (searchCompute.runtime.end - searchCompute.runtime.start) * device.timestampPeriod();
    auto durationMs = duration * 1e-6f;
    spdlog::info("GPU search result found in {} ms", durationMs);

    int next = 0;
    int pIndex = 0;
    auto gSearchResults = reinterpret_cast<int*>(searchResultBuffer.map());
    while(pIndex != -1){
        pIndex = gSearchResults[next++];
        points[gSearchResults[next]].color = glm::vec4(0, 1, 0, 1);
    }
    searchResultBuffer.unmap();
}

void SpacePartitioning::runCpuSearch() {
    clearQueryPoints();
    spdlog::info("search radius: {}", searchArea.radius);
    auto start = std::chrono::high_resolution_clock::now();

    searchResults = kdtree::search_loop(tree, {points, static_cast<size_t>(numPoints)}, searchArea, numSearchPoints);

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    spdlog::info("num points found {} in {} ms", searchResults.size(), duration);
    for (auto p: searchResults) {
        p->color = glm::vec4(0, 1, 0, 1);
    }

    if (debugSearch) {
        std::vector<Point *> bruteforceResults;
        for (int i = 0; i < numPoints; i++) {
            auto p = points[i].position;
            auto x = searchArea.position;
            auto d = x - p;
            auto d2 = searchArea.radius * searchArea.radius;
            auto delta2 = dot(d, d);
            if (delta2 < d2) {
                bruteforceResults.push_back(&points[i]);
            }
        }


        std::sort(bruteforceResults.begin(), bruteforceResults.end(),
                  [](auto a, auto b) { return std::distance(a, b) > 0; });
        std::sort(searchResults.begin(), searchResults.end(),
                  [](auto a, auto b) { return std::distance(a, b) > 0; });

        for (auto p: bruteforceResults) {
            auto found = std::any_of(searchResults.begin(), searchResults.end(),
                                     [&](auto sp) { return sp == p; });
            if (!found) {
                missedPoints.push_back(p);
            }
        }

        std::vector<int> missingIndex;
        for (auto p: missedPoints) {
            p->color = {1, 0, 0, 1};
            for (int i = 0; i < numPoints; i++) {
                if (p == &points[i]) {
                    missingIndex.push_back(i);
                }
            }
        }
        if (!missingIndex.empty()) {
            spdlog::error("points expected in search area: {}", missingIndex);
        }
    }
}

void SpacePartitioning::clearQueryPoints() {
    for(auto p : searchResults) {
        p->color = glm::vec4(1);
    }
    for(auto p : missedPoints) {
        p->color = glm::vec4(1);
    }
    searchResults.clear();
    missedPoints.clear();

    int next = 0;
    int pIndex = 0;
    auto gSearchResults = reinterpret_cast<int*>(searchResultBuffer.map());
    while(pIndex != -1){
        pIndex = gSearchResults[next++];
        points[gSearchResults[next]].color = glm::vec4(1);
    }
    searchResultBuffer.unmap();
}

int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.wideLines = VK_TRUE;
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = SpacePartitioning{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}