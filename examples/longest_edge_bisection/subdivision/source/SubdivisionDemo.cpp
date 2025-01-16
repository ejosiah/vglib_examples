#include "SubdivisionDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"

#include "leb/leb.hpp"

SubdivisionDemo::SubdivisionDemo(const Settings& settings) : VulkanBaseApp("subdivision", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("subdivision");
    fileManager().addSearchPathFront("subdivision/data");
    fileManager().addSearchPathFront("subdivision/spv");
    fileManager().addSearchPathFront("subdivision/models");
    fileManager().addSearchPathFront("subdivision/textures");
    fileManager().addSearchPath("../../data/shaders");
}

void SubdivisionDemo::initApp() {
    initCBT();
    initCamera();
    createBuffers();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void SubdivisionDemo::initCamera() {
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

void SubdivisionDemo::beforeDeviceCreation() {
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
}

void SubdivisionDemo::createDescriptorPool() {
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


void SubdivisionDemo::createDescriptorSetLayouts() {
    leb.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("leb_descriptor_set_layout")
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
        .createLayout();
}

void SubdivisionDemo::updateDescriptorSets(){
    leb.descriptorSet = descriptorPool.allocate({ leb.descriptorSetLayout }).front();
    
    auto writes = initializers::writeDescriptorSets<4>();
    
    writes[0].dstSet = leb.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo lebInfo{ leb.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &lebInfo;

    writes[1].dstSet = leb.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo drawInfo{ drawBuffer, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &drawInfo;

    writes[2].dstSet = leb.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchInfo{ dispatchBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &dispatchInfo;

    writes[3].dstSet = leb.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo cbtInfo{ cbtInfoBuffer, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &cbtInfo;
    
    device.updateDescriptorSets(writes);
}

void SubdivisionDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SubdivisionDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SubdivisionDemo::createRenderPipeline() {
    //    @formatter:off
    auto builder = prototypes->cloneScreenSpaceGraphicsPipeline();
    render.target.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("target.vert.spv"))
                .fragmentShader(resource("target.frag.spv"))
            .vertexInputState().clear()
            .inputAssemblyState()
                .points()
            .depthStencilState()
                .compareOpAlways()
            .rasterizationState()
                .cullNone()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2))
            .name("target_render")
            .build(render.target.layout);

    render.triangle.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("triangle.vert.spv"))
                    .addSpecialization(static_cast<int>(mode), 0)
                .geometryShader(resource("triangle.geom.spv"))
                    .addSpecialization(width, 0)
                    .addSpecialization(height, 1)
                .fragmentShader(resource("triangle.frag.spv"))
            .vertexInputState().clear()
            .inputAssemblyState()
                .triangles()
            .rasterizationState()
                .cullNone()
            .layout().clear()
                .addDescriptorSetLayout(leb.descriptorSetLayout)
            .name("triangle_render")
        .build(render.triangle.layout);
    //    @formatter:on
}

void SubdivisionDemo::createComputePipeline() {
    static int CbtID = 0;
    static int WorkGroupSize = 256;
    static std::vector<int> constantData;
    auto mapEntries = std::vector<VkSpecializationMapEntry>{};

    constantData.clear();
    mapEntries.clear();

    constantData.emplace_back(CbtID);

    mapEntries.emplace_back(0, 0, sizeof(int));

    auto specializationInfo = VkSpecializationInfo{
        .mapEntryCount = 1,
        .pMapEntries = mapEntries.data(),
        .dataSize = constantData.size() * sizeof(int),
        .pData = constantData.data()
    };

    // LEB dispatch
    auto module = device.createShaderModule(resource("leb_dispatcher.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;

    compute.lebDispatcher.layout = device.createPipelineLayout({ leb.descriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.lebDispatcher.layout.handle;

    compute.lebDispatcher.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("leb_dispatcher_layout", compute.lebDispatcher.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("leb_dispatcher_pipeline", compute.lebDispatcher.pipeline.handle);

    // CBT dispatch
    module = device.createShaderModule(resource("cbt_dispatcher.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;

    compute.cbtDispatcher.layout = device.createPipelineLayout({ leb.descriptorSetLayout });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.cbtDispatcher.layout.handle;
    compute.cbtDispatcher.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("cbt_dispatcher_layout", compute.cbtDispatcher.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("cbt_dispatcher_pipeline", compute.cbtDispatcher.pipeline.handle);

    // CBT Info
    module = device.createShaderModule(resource("cbt_info.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;

    compute.cbtInfo.layout = device.createPipelineLayout({ leb.descriptorSetLayout });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.cbtInfo.layout.handle;
    compute.cbtInfo.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("cbt_info_layout", compute.cbtInfo.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("cbt_info_pipeline", compute.cbtInfo.pipeline.handle);

    // Sum reduce pre-pass
    mapEntries.emplace_back(1, sizeof(int), sizeof(int));
    constantData.emplace_back(WorkGroupSize);
    specializationInfo.mapEntryCount = mapEntries.size();
    specializationInfo.pMapEntries = mapEntries.data();
    specializationInfo.pData = constantData.data();
    specializationInfo.dataSize = constantData.size() * sizeof(int);

    module = device.createShaderModule(resource("cbt_sum_reduce_prepass.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.sumReducePrepass.layout.handle;
    compute.sumReducePrepass.layout = device.createPipelineLayout(
            { leb.descriptorSetLayout }
            , { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.sumReducePrepass.pass) } });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.sumReducePrepass.layout.handle;
    compute.sumReducePrepass.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("sum_reduce_prepass_layout", compute.sumReducePrepass.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sum_reduce_prepass_pipeline", compute.sumReducePrepass.pipeline.handle);

    // Sum reduce
    module = device.createShaderModule(resource("cbt_sum_reduce.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.sumReduce.layout.handle;
    compute.sumReduce.layout = device.createPipelineLayout(
            { leb.descriptorSetLayout }
            , { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.sumReduce.pass) } });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.sumReduce.layout.handle;
    compute.sumReduce.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("sum_reduce_layout", compute.sumReduce.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sum_reduce_pipeline", compute.sumReduce.pipeline.handle);


    // Subdivision
    mapEntries.emplace_back(2, sizeof(int) * 2, sizeof(int));
    constantData.emplace_back(static_cast<int>(mode));

    specializationInfo.mapEntryCount = mapEntries.size();
    specializationInfo.pMapEntries = mapEntries.data();
    specializationInfo.pData = constantData.data();
    specializationInfo.dataSize = constantData.size() * sizeof(int);

    module = device.createShaderModule(resource("subdivision.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;
    compute.subdivision.layout = device.createPipelineLayout(
            { leb.descriptorSetLayout }
            , { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.subdivision.constants) } });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.subdivision.layout.handle;
    compute.subdivision.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("subdivision_layout", compute.subdivision.layout.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("subdivision_pipeline", compute.subdivision.pipeline.handle);

}


void SubdivisionDemo::onSwapChainDispose() {
    dispose(render.target.pipeline);
    dispose(render.triangle.pipeline);
}

void SubdivisionDemo::onSwapChainRecreation() {
    initCBT();
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *SubdivisionDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    transferCBT(commandBuffer);
    lebDispatcher(commandBuffer);

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
    {
        renderTarget(commandBuffer);
        renderTriangle(commandBuffer);
        renderUI(commandBuffer);
    }
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SubdivisionDemo::renderUI(VkCommandBuffer commandBuffer) {
    static auto iMode = 0;
    static auto iBackend = 0;
    static auto modes_labels = std::array<const char*, 2>{ "Triangle", "Square" };
    static auto backend_labels = std::array<const char*, 2>{ "GPU", "CPU"};

    iMode = static_cast<int>(mode);
    iBackend = static_cast<int>(backend);

    static auto should_reload = false;

    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::Begin("Settings");
    {
        ImGui::SetWindowSize({0, 0});
        should_reload |= ImGui::Combo("Mode", &iMode, modes_labels.data(), modes_labels.size());
        should_reload |= ImGui::Combo("Backend", &iBackend, backend_labels.data(), backend_labels.size());

        ImGui::SliderFloat("targetX", &target.x, -0.1, 1.1);
        ImGui::SliderFloat("targetY", &target.y, -0.1, 1.1);

        should_reload |= ImGui::SliderFloat("MaxDepth", &maxDepth, 6, 30, "%.0f");
        should_reload |= ImGui::Button("Reset");

        ImGui::Separator();
        ImGui::Text("Nodes: %u", leb.cbt_info.nodeCount);

    }
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);

    mode = static_cast<Mode>(iMode);
    backend = static_cast<Backend>(iBackend);

    if(should_reload & !ImGui::IsAnyItemActive()) {
        onIdle([&]{ invalidateSwapChain(); });
        should_reload = false;
    }
}

void SubdivisionDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();

    updateSubdivision();
}

void SubdivisionDemo::checkAppInputs() {
    camera->processInput();
}

void SubdivisionDemo::cleanup() {
}

void SubdivisionDemo::onPause() {
    VulkanBaseApp::onPause();
}

void SubdivisionDemo::renderTarget(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.target.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.target.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0 , sizeof(glm::vec2), &target);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, dummyBuffer , &offset);
    vkCmdDraw(commandBuffer, 1, 1, 0, 0);
}

void SubdivisionDemo::renderTriangle(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.triangle.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.triangle.layout.handle, 0, 1, &leb.descriptorSet, 0, nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, dummyBuffer , &offset);
    vkCmdDrawIndirect(commandBuffer, drawBuffer, 0, 1, sizeof(VkDrawIndirectCommand));
}

void SubdivisionDemo::createBuffers() {
    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    dummyBuffer = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int) * 3, "dummy_buffer");
    drawBuffer = device.createBuffer(usage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(VkDrawIndirectCommand), "draw_triangle");
    dispatchBuffer = device.createBuffer(usage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(VkDispatchIndirectCommand), "dispatch_buffer");
}

void SubdivisionDemo::initCBT() {
    leb.cbt = cbt::Tree{ static_cast<int64_t>(maxDepth), initMaxDepth};
    auto heap = leb.cbt.getHeap();
    leb.gpu = device.createDeviceLocalBuffer(heap.data(), heap.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device.setName<VK_OBJECT_TYPE_BUFFER>("cbt_heap", leb.gpu.buffer);
    transferBuffer = device.createStagingBuffer(std::max(sizeof(CbtInfo), leb.cbt.getHeap().size()));
    transferLink = transferBuffer.map();

    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    cbtInfoBuffer = device.createBuffer(usage, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(CbtInfo), "cbt_info");
}

void SubdivisionDemo::mergeCallback(cbt::Tree & cbt, const cbt::Node &node, const void *userData) {
    const auto app = reinterpret_cast<const SubdivisionDemo*>(userData);

    float baseFaceVertices[][3] = {
            {0.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f}
    };
    float topFaceVertices[][3] = {
            {0.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f}
    };

    if (app->mode == Mode::Triangle) {
        leb_DiamondParent diamondParent = leb_DecodeDiamondParent(node);

        leb_DecodeNodeAttributeArray(diamondParent.base, 2, baseFaceVertices);
        leb_DecodeNodeAttributeArray(diamondParent.top, 2, topFaceVertices);

        if (!app->isInside(baseFaceVertices) && !app->isInside(topFaceVertices)) {
            leb_MergeNode(&cbt.delegate(), node, diamondParent);
        }
    } else {
        leb_DiamondParent diamondParent = leb_DecodeDiamondParent_Square(node);

        leb_DecodeNodeAttributeArray_Square(diamondParent.base, 2, baseFaceVertices);
        leb_DecodeNodeAttributeArray_Square(diamondParent.top, 2, topFaceVertices);

        if (!app->isInside(baseFaceVertices) && !app->isInside(topFaceVertices)) {
            leb_MergeNode_Square(&cbt.delegate(), node, diamondParent);
        }
    }
}


void SubdivisionDemo::splitCallback(cbt::Tree & cbt, const cbt::Node &node,  const void* userData) {
    float faceVertices[][3] = {
            {0.0f, 0.0f, 1.0f},
            {1.0f, 0.0f, 0.0f}
    };

    const auto app = reinterpret_cast<const SubdivisionDemo*>(userData);
    if (app->mode == Mode::Triangle) {
        leb_DecodeNodeAttributeArray(node, 2, faceVertices);

        if (app->isInside(faceVertices)) {
            leb_SplitNode(&cbt.delegate(), node);
        }
    } else {
        leb_DecodeNodeAttributeArray_Square(node, 2, faceVertices);

        if (app->isInside(faceVertices)) {
            leb_SplitNode_Square(&cbt.delegate(), node);
        }
    }
}

bool SubdivisionDemo::isInside(const float (*faceVertices)[3]) const {
    static auto Wedge = [](const auto a, const auto b) {
        return a[0] * b[1] - a[1] * b[0];
    };

    float target[2] = {this->target.x, this->target.y};
    float v1[2] = {faceVertices[0][0], faceVertices[1][0]};
    float v2[2] = {faceVertices[0][1], faceVertices[1][1]};
    float v3[2] = {faceVertices[0][2], faceVertices[1][2]};
    float x1[2] = {v2[0] - v1[0], v2[1] - v1[1]};
    float x2[2] = {v3[0] - v2[0], v3[1] - v2[1]};
    float x3[2] = {v1[0] - v3[0], v1[1] - v3[1]};
    float y1[2] = {target[0] - v1[0], target[1] - v1[1]};
    float y2[2] = {target[0] - v2[0], target[1] - v2[1]};
    float y3[2] = {target[0] - v3[0], target[1] - v3[1]};
    float w1 = Wedge(x1, y1);
    float w2 = Wedge(x2, y2);
    float w3 = Wedge(x3, y3);

    return (w1 >= 0.0f) && (w2 >= 0.0f) && (w3 >= 0.0f);
}

void SubdivisionDemo::updateSubdivision() {
    static int pingPong = 0;

    if(backend == Backend::CPU) {
        if(pingPong == 0) {
            leb.cbt.update(splitCallback, this);
        }else {
            leb.cbt.update(mergeCallback, this);
        }
        auto heap = leb.cbt.getHeap();
        transferBuffer.copy(heap.data(), heap.size());
        leb.cbt_info.nodeCount = leb.cbt.nodeCount();
    }else {
        // subdivide
        device.graphicsCommandPool().oneTimeCommand([this](auto commandBuffer){
            cbtDispatch(commandBuffer);
            lebSubdivision(commandBuffer, pingPong);
            getCbtInfo(commandBuffer);
        });

        std::memcpy(&leb.cbt_info, transferLink, sizeof(CbtInfo));

        // sum reduction
        device.graphicsCommandPool().oneTimeCommand([this](auto commandBuffer){
            sumReducePrePass(commandBuffer);
            sumReduceCbt(commandBuffer);
        });
    }

    pingPong = 1 - pingPong;
}

void SubdivisionDemo::transferCBT(VkCommandBuffer commandBuffer) {
    if(backend != Backend::CPU) return;

    static VkBufferCopy region{0, 0};
    region.size = leb.gpu.size;
    vkCmdCopyBuffer(commandBuffer, transferBuffer, leb.gpu, 1, &region);
    hostToGpuTransferBarrier(commandBuffer);

}

void SubdivisionDemo::transferCBTToHost(VkCommandBuffer commandBuffer) {
    static VkBufferCopy region{0, 0};
    region.size = sizeof(CbtInfo);
    vkCmdCopyBuffer(commandBuffer, cbtInfoBuffer, transferBuffer, 1, &region);
    gpuToHostTransferBarrier(commandBuffer);
}

void SubdivisionDemo::lebDispatcher(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.lebDispatcher.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.lebDispatcher.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    computeToDrawBarrier(commandBuffer);
}

void SubdivisionDemo::computeToDrawBarrier(VkCommandBuffer commandBuffer) {
    static VkMemoryBarrier2 memoryBarrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
    };

    static VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memoryBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void SubdivisionDemo::hostToGpuTransferBarrier(VkCommandBuffer commandBuffer) {
    static VkMemoryBarrier2 memoryBarrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };

    static VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memoryBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void SubdivisionDemo::gpuToHostTransferBarrier(VkCommandBuffer commandBuffer) {
    static VkMemoryBarrier2 memoryBarrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_HOST_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };

    static VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memoryBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void SubdivisionDemo::computeToComputeBarrier(VkCommandBuffer commandBuffer) {
    static VkMemoryBarrier2 memoryBarrier{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };

    static VkDependencyInfo dependencyInfo{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memoryBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void SubdivisionDemo::cbtDispatch(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.cbtDispatcher.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.cbtDispatcher.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    computeToComputeBarrier(commandBuffer);
}

void SubdivisionDemo::lebSubdivision(VkCommandBuffer commandBuffer, int pingPong) {
    compute.subdivision.constants.target = target;
    compute.subdivision.constants.updateFlag = pingPong;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.subdivision.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.subdivision.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
    vkCmdPushConstants(commandBuffer, compute.subdivision.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.subdivision.constants), &compute.subdivision.constants);
    vkCmdDispatchIndirect(commandBuffer, dispatchBuffer, 0);
    computeToComputeBarrier(commandBuffer);
}

void SubdivisionDemo::getCbtInfo(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.cbtInfo.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.cbtInfo.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    transferCBTToHost(commandBuffer);
}


void SubdivisionDemo::sumReducePrePass(VkCommandBuffer commandBuffer) {
    auto itr = leb.cbt_info.maxDepth;
    auto cnt = ((1 << itr) >> 5);
    auto numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
    compute.sumReducePrepass.pass = itr;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sumReducePrepass.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sumReducePrepass.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
    vkCmdPushConstants(commandBuffer, compute.sumReducePrepass.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.sumReducePrepass.pass), &compute.sumReducePrepass.pass);
    vkCmdDispatch(commandBuffer, numGroup, 1, 1);
    computeToComputeBarrier(commandBuffer);
}

void SubdivisionDemo::sumReduceCbt(VkCommandBuffer commandBuffer) {
    for(int itr = leb.cbt_info.maxDepth - 6; itr >= 0; --itr) {
        auto cnt = 1 << itr;
        auto numGroup = (cnt >= 256) ? (cnt >> 8) : 1;
        compute.sumReduce.pass = itr;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sumReduce.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.sumReduce.layout.handle, 0, 1, &leb.descriptorSet, 0,nullptr);
        vkCmdPushConstants(commandBuffer, compute.sumReduce.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute.sumReduce.pass), &compute.sumReduce.pass);
        vkCmdDispatch(commandBuffer, numGroup, 1, 1);
        computeToComputeBarrier(commandBuffer);
    }
}

int main(){
    try{
        fs::current_path("../../../../../examples/longest_edge_bisection");
        Settings settings;
        settings.width = 720;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.enabledFeatures.geometryShader = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = SubdivisionDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}