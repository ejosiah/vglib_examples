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
            .createLayout();
}

void SubdivisionDemo::updateDescriptorSets(){
    leb.descriptorSet = descriptorPool.allocate({ leb.descriptorSetLayout }).front();
    
    auto writes = initializers::writeDescriptorSets();
    
    writes[0].dstSet = leb.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo lebInfo{ leb.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &lebInfo;
    
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


void SubdivisionDemo::onSwapChainDispose() {
    dispose(render.target.pipeline);
    dispose(render.triangle.pipeline);
}

void SubdivisionDemo::onSwapChainRecreation() {
    initCBT();
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *SubdivisionDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    transferCBT(commandBuffer);

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
        ImGui::Combo("Backend", &iBackend, backend_labels.data(), backend_labels.size());

        ImGui::SliderFloat2("target", &target.x, -0.1, 1.1);

        should_reload |= ImGui::SliderFloat("MaxDepth", &maxDepth, 6, 30, "%.0f");
        should_reload |= ImGui::Button("Reset");

        ImGui::Separator();
        ImGui::Text("Nodes: %lld", leb.cbt.nodeCount());

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
    vkCmdDraw(commandBuffer, 3, leb.cbt.nodeCount(), 0, 0);
}

void SubdivisionDemo::createBuffers() {
    dummyBuffer = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int) * 3, "dummy_buffer");
}

void SubdivisionDemo::initCBT() {
    leb.cbt = cbt::Tree{ static_cast<int64_t>(maxDepth), initMaxDepth};
    auto heap = leb.cbt.getHeap();
    leb.gpu = device.createDeviceLocalBuffer(heap.data(), heap.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    device.setName<VK_OBJECT_TYPE_BUFFER>("cbt_heap", leb.gpu.buffer);
    transferBuffer = device.createStagingBuffer(leb.cbt.getHeap().size());
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
    }

    pingPong = 1 - pingPong;
}

void SubdivisionDemo::transferCBT(VkCommandBuffer commandBuffer) {
    if(backend != Backend::CPU) return;

    static VkMemoryBarrier2 memoryBarrier{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
    };

    static VkDependencyInfo dependencyInfo{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &memoryBarrier
    };

    static VkBufferCopy region{0, 0};
    region.size = leb.gpu.size;
    vkCmdCopyBuffer(commandBuffer, transferBuffer, leb.gpu, 1, &region);
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
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