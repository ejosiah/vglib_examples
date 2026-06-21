#include "Voxelization.hpp"
#include "AppContext.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "Barrier.hpp"

namespace {
    constexpr auto MaxVoxelTransforms = 128u * 128u * 128u;

    struct HybridClassifierConstants {
        glm::mat4 worldToVoxelTransform{1};
        float cutoffArea{};
        uint32_t voxelSize{};
        uint32_t triangleCount{};
        uint32_t _pad{};
    };
}

Voxelization::Voxelization(const Settings& settings) : VulkanBaseApp("Computatinal Voxelization", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("voxelization");
    fileManager().addSearchPathFront("voxelization/data");
    fileManager().addSearchPathFront("voxelization/spv");
    fileManager().addSearchPathFront("voxelization/models");
    fileManager().addSearchPathFront("voxelization/textures");
}

void Voxelization::initApp() {
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initFloor();
    initCube();
    loadModel();
    initVoxelData();
    initCamera();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createVoxelStorage();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
}

void Voxelization::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.velocity = glm::vec3{5};
    cameraSettings.acceleration = glm::vec3(5);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.horizontalFov = true;
    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({-5, 2, 3}, {0, 0, 0}, {0, 1, 0});
}


void Voxelization::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 3> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void Voxelization::createDescriptorSetLayouts() {
    voxels.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("voxel_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
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
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    voxels.descriptorSet = descriptorPool.allocate( { voxels.descriptorSetLayout }).front();

    model.hybrid.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("hybrid_classifier_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    model.hybrid.descriptorSet = descriptorPool.allocate({ model.hybrid.descriptorSetLayout }).front();
}

void Voxelization::updateDescriptorSets(){
    updateHybridClassifierDescriptorSet();
}

void Voxelization::updateHybridClassifierDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<6>();
    std::array<VkDescriptorBufferInfo, 6> bufferInfo{
        {
            { model.vertices, 0, VK_WHOLE_SIZE },
            { model.indices, 0, VK_WHOLE_SIZE },
            { model.hybrid.triangleIndices, 0, VK_WHOLE_SIZE },
            { model.hybrid.fragmentIndices, 0, VK_WHOLE_SIZE },
            { model.hybrid.stats, 0, VK_WHOLE_SIZE },
            { model.hybrid.drawCommands, 0, VK_WHOLE_SIZE },
        }
    };

    for(uint32_t i = 0; i < COUNT(writes); ++i) {
        writes[i].dstSet = model.hybrid.descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &bufferInfo[i];
    }

    device.updateDescriptorSets(writes);
}

void Voxelization::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Voxelization::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Voxelization::createRenderPipeline() {
    //    @formatter:off
    auto builder = prototypes->cloneGraphicsPipeline();
    pipelines.triangle.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("voxelizeTriangleParallel.vert.spv"))
                    .geometryShader(resource("voxelizeTriangleParallel.geom.spv"))
                .rasterizationState()
                    .enableRasterizerDiscard()
                .dynamicState()
                    .primitiveTopology()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4))
                    .addDescriptorSetLayout(voxels.descriptorSetLayout)
                .name("triangle_parallel_voxelization")
                .build(pipelines.triangle.layout);

    pipelines.fragment.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("voxelizeFragmentParallel.vert.spv"))
                    .geometryShader(resource("voxelizeFragmentParallel.geom.spv"))
                    .fragmentShader(resource("voxelizeFragmentParallel.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .disableDepthTest()
                    .disableDepthWrite()
                    .compareOpAlways()
                .colorBlendState()
                    .attachment()
                    .clear()
                    .colorWriteMask(0)
                    .add()
                .dynamicState()
                    .primitiveTopology()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(glm::mat4) * 2)
                    .addDescriptorSetLayout(voxels.descriptorSetLayout)
                .name("fragment_parallel_voxelization")
                .build(pipelines.fragment.layout);

	auto builder1 = prototypes->cloneGraphicsPipeline();
	pipelines.render.pipeline =
        builder1
			.shaderStage()
				.vertexShader(resource("solid.vert.spv"))
                .fragmentShader(resource("solid.frag.spv"))
            .inputAssemblyState()
                .triangleStrip()
            .dynamicState()
                .primitiveTopology()
                .cullMode()
            .layout()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
			.name("solid_render")
			.build(pipelines.render.layout);

    auto builder2 = prototypes->cloneGraphicsPipeline();
    pipelines.rayMarch.pipeline =
        builder2
			.shaderStage()
				.vertexShader(resource("ray_march.vert.spv"))
                .fragmentShader(resource("ray_march.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
            .inputAssemblyState()
                .triangleStrip()
			.rasterizationState()
				.cullNone()
            .depthStencilState()
                .compareOpLess()
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera) + sizeof(glm::vec4) * 2)
                .addDescriptorSetLayout(voxels.descriptorSetLayout)
			.name("ray_march")
			.build(pipelines.rayMarch.layout);
    //    @formatter:on

}

void Voxelization::createComputePipelines() {
    auto module = device.createShaderModule(resource("gen_voxel_transforms.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    pipelines.genVoxelTransforms.layout = device.createPipelineLayout( {  voxels.descriptorSetLayout } );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.genVoxelTransforms.layout.handle;

    pipelines.genVoxelTransforms.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_voxel_xforms", pipelines.genVoxelTransforms.pipeline.handle);

    module = device.createShaderModule(resource("hybrid_classify.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    VkPushConstantRange constants{};
    constants.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    constants.offset = 0;
    constants.size = sizeof(HybridClassifierConstants);

    pipelines.hybridClassifier.layout = device.createPipelineLayout({ model.hybrid.descriptorSetLayout }, { constants });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = pipelines.hybridClassifier.layout.handle;

    pipelines.hybridClassifier.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("hybrid_triangle_classifier", pipelines.hybridClassifier.pipeline.handle);
}


void Voxelization::onSwapChainDispose() {
    dispose(pipelines.triangle.pipeline);
    dispose(pipelines.render.pipeline);
    dispose(pipelines.rayMarch.pipeline);
}

void Voxelization::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    initFloor();
}

void Voxelization::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

VkCommandBuffer *Voxelization::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    updateUI();

    if(recreateVoxelStorage) {
        device.wait();
        createVoxelStorage();
        recreateVoxelStorage = false;
        refreshVoxel = true;
    }

    if(method == Method::Hybrid && model.hybrid.dirty) {
        device.wait();
    }

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    if(method == Method::Hybrid && model.hybrid.dirty) {
        classifyHybridTriangles(commandBuffer);
    }

    if(refreshVoxel) {
        clearVoxels(commandBuffer);
    }

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

    floor.render(commandBuffer, *camera);

    if(renderType == RenderType::Default){
        renderModel(commandBuffer);
    }else if(renderType == RenderType::Voxels) {
        renderVoxels(commandBuffer);
    }else if(renderType == RenderType::RayMarch) {
        rayMarch(commandBuffer);
    }

    if(refreshVoxel) {
        voxelize(commandBuffer);
    }
    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    if(refreshVoxel) {
        generateVoxelTransforms(commandBuffer);
        refreshVoxel = false;
    }

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Voxelization::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    auto cam = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{}, pos: {}", title, camera->position()).c_str());
}

void Voxelization::checkAppInputs() {
    camera->processInput();
}

void Voxelization::cleanup() {
    AppContext::shutdown();
}

void Voxelization::onPause() {
    VulkanBaseApp::onPause();
}

void Voxelization::initFloor() {
    floor = Floor{ device, *prototypes };
    floor.init();
}

void Voxelization::loadModel() {
    std::vector<mesh::Mesh> meshes;
    mesh::load(meshes, resource("cow.ply"));
    auto& mesh = meshes.front();
    bounds.min = glm::vec4(mesh.bounds.min, 0);
    bounds.max = glm::vec4(mesh.bounds.max, 0);
    spdlog::info("bounds [min : {}, max: {} ]", mesh.bounds.min, mesh.bounds.max);

    auto dim = mesh.bounds.max - mesh.bounds.min;
    auto invMaxAxis = 1.f/glm::max(dim.x, glm::max(dim.y, dim.z));
    voxels.transform = glm::scale(glm::mat4{1}, glm::vec3(invMaxAxis));
    voxels.transform = glm::translate(voxels.transform, -mesh.bounds.min);

    auto tmin = voxels.transform * glm::vec4(mesh.bounds.min, 1);
    auto tmax = voxels.transform * glm::vec4(mesh.bounds.max, 1);
    spdlog::info("bounds [min : {}, max: {} ]", tmin.xyz(), tmax.xyz());

    model.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    model.indices = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    createHybridClassificationBuffers();
}

void Voxelization::initCube() {
    auto c = primitives::cube();
    cube.vertices = device.createDeviceLocalBuffer(c.vertices.data(), BYTE_SIZE(c.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.indices = device.createDeviceLocalBuffer(c.indices.data(), BYTE_SIZE(c.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Voxelization::initVoxelData() {
    VoxelData data{};
    data.maxVoxels = static_cast<int>(MaxVoxelTransforms);
    voxels.dataBuffer = device.createCpuVisibleBuffer(&data, sizeof(data), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    voxels.data = reinterpret_cast<VoxelData*>(voxels.dataBuffer.map());
    voxels.transforms = device.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        sizeof(glm::mat4) * MaxVoxelTransforms,
        "voxel_transforms_buffer");
}

void Voxelization::createHybridClassificationBuffers() {
    model.hybrid.sourceTriangleCount = model.indices.sizeAs<uint32_t>() / 3;
    const auto indexBufferSize = model.indices.size;

    model.hybrid.triangleIndices = device.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        indexBufferSize,
        "hybrid_triangle_parallel_indices");

    model.hybrid.fragmentIndices = device.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        indexBufferSize,
        "hybrid_fragment_parallel_indices");

    model.hybrid.stats = device.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_TO_CPU,
        sizeof(HybridStats),
        "hybrid_classifier_stats");
    model.hybrid.statsData = reinterpret_cast<HybridStats*>(model.hybrid.stats.map());

    model.hybrid.drawCommands = device.createBuffer(
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY,
        sizeof(VkDrawIndexedIndirectCommand) * 2,
        "hybrid_classifier_draw_commands");

    model.hybrid.dirty = true;
}

void Voxelization::renderModel(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.pipeline.handle);
    AppContext::bindInstanceDescriptorSets(commandBuffer, pipelines.render.layout);
    camera->push(commandBuffer, pipelines.render.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    vkCmdDrawIndexed(commandBuffer, model.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void Voxelization::renderVoxels(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.render.layout.handle, 0, 1, &voxels.transformsDescriptorSet, 0, 0);

    camera->push(commandBuffer, pipelines.render.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cube.indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetCullMode(commandBuffer, VK_CULL_MODE_BACK_BIT);
    auto numVoxels = static_cast<uint32_t>(glm::max(voxels.data->numVoxels, 0));
    vkCmdDrawIndexed(commandBuffer, cube.indices.sizeAs<uint32_t>(), glm::min(numVoxels, MaxVoxelTransforms), 0, 0, 0);
}

void Voxelization::rayMarch(VkCommandBuffer commandBuffer) {
    static uint32_t pcSize = sizeof(Camera) + sizeof(bounds);
    static std::vector<char> pc(pcSize);
    std::memcpy(pc.data(), &camera->camera, sizeof(Camera));
    std::memcpy(pc.data() + sizeof(Camera), &bounds.min.x, sizeof(bounds));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.rayMarch.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.rayMarch.layout.handle, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pc.size(), pc.data());
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void Voxelization::updateUI() {
    ImGui::Begin("Voxelization");
    ImGui::SetWindowSize({0, 0});
    ImGui::Text("Method:");
    ImGui::Indent(16);

    int methodOpt = static_cast<int>(method);
    ImGui::RadioButton("Triangle", &methodOpt, static_cast<int>(Method::TriangleParallel));
    ImGui::SameLine();
    ImGui::RadioButton("Fragment", &methodOpt, static_cast<int>(Method::FragmentParallel));
    ImGui::SameLine();
    ImGui::RadioButton("Hybrid", &methodOpt, static_cast<int>(Method::Hybrid));
    ImGui::Indent(-16);

    float hybridCutoff = model.hybrid.cutoffArea;
    if(method == Method::Hybrid) {
        ImGui::Text("Hybrid:");
        ImGui::Indent(16);
        ImGui::SliderFloat("Cutoff", &hybridCutoff, 0.0f, 50.0f, "%.2f voxels^2");
        const auto smallTriangles = model.hybrid.statsData ? model.hybrid.statsData->triangleCount : 0u;
        const auto largeTriangles = model.hybrid.statsData ? model.hybrid.statsData->fragmentCount : 0u;
        ImGui::Text("Small / large: %u / %u", smallTriangles, largeTriangles);
        ImGui::Indent(-16);
    }

    ImGui::Text("Voxel Size:");
    ImGui::Indent(16);
    int vSize = static_cast<int>(voxels.size);
    ImGui::RadioButton("128", &vSize, 128);
    ImGui::SameLine();
    ImGui::RadioButton("256", &vSize, 256);
    ImGui::SameLine();
    ImGui::RadioButton("512", &vSize, 512);
    ImGui::InputInt("Resolution", &vSize, 1, 8);
    vSize = glm::clamp(vSize, 1, 512);
    ImGui::Indent(-16);

    ImGui::Text("Render:");
    ImGui::Indent(16);
    int renderOpt = static_cast<int>(renderType);
    ImGui::RadioButton("Default", &renderOpt, static_cast<int>(RenderType::Default));
    ImGui::SameLine();
    ImGui::RadioButton("Voxels", &renderOpt, static_cast<int>(RenderType::Voxels));
    ImGui::SameLine();
    ImGui::RadioButton("RayMatch", &renderOpt, static_cast<int>(RenderType::RayMarch));
    renderType = static_cast<RenderType>(renderOpt);
    ImGui::Indent(-16);

    ImGui::End();

    const auto newMethod = static_cast<Method>(methodOpt);
    const auto newSize = static_cast<uint32_t>(vSize);

    if(newMethod != method) {
        method = newMethod;
        refreshVoxel = true;
        model.hybrid.dirty = method == Method::Hybrid;
    }

    if(newSize != voxels.size) {
        voxels.size = newSize;
        recreateVoxelStorage = true;
        model.hybrid.dirty = true;
    }

    if(hybridCutoff != model.hybrid.cutoffArea) {
        model.hybrid.cutoffArea = hybridCutoff;
        model.hybrid.dirty = true;
        refreshVoxel = true;
    }
}

void Voxelization::renderUI(VkCommandBuffer commandBuffer) {
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void Voxelization::clearVoxels(VkCommandBuffer commandBuffer) {
    VkImageSubresourceRange range = DEFAULT_SUB_RANGE;

    VkImageMemoryBarrier toTransfer{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.image = voxels.texture.image;
    toTransfer.subresourceRange = range;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toTransfer);

    VkClearColorValue clearValue{};
    clearValue.uint32[0] = 0u;
    vkCmdClearColorImage(commandBuffer, voxels.texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

    VkImageMemoryBarrier toGeneral{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.image = voxels.texture.image;
    toGeneral.subresourceRange = range;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &toGeneral);
}

void Voxelization::voxelize(VkCommandBuffer commandBuffer) {
    switch(method) {
        case Method::TriangleParallel:
            triangleParallelVoxelization(commandBuffer);
            break;
        case Method::FragmentParallel:
            fragmentParallelVoxelization(commandBuffer);
            break;
        case Method::Hybrid:
            hybridVoxelization(commandBuffer);
            break;
    }
}

void Voxelization::triangleParallelVoxelization(VkCommandBuffer commandBuffer) {
    triangleParallelVoxelization(commandBuffer, model.indices, model.indices.sizeAs<uint32_t>());
}

void Voxelization::triangleParallelVoxelization(VkCommandBuffer commandBuffer, const VulkanBuffer& indices, uint32_t indexCount) {
    if(indexCount == 0 || indices.empty()) return;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.triangle.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(voxels.transform));
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void Voxelization::fragmentParallelVoxelization(VkCommandBuffer commandBuffer) {
    fragmentParallelVoxelization(commandBuffer, model.indices, model.indices.sizeAs<uint32_t>());
}

void Voxelization::fragmentParallelVoxelization(VkCommandBuffer commandBuffer, const VulkanBuffer& indices, uint32_t indexCount) {
    if(indexCount == 0 || indices.empty()) return;

    static std::array<glm::mat4, 2> pushConstants{};
    VkDeviceSize offset = 0;
    pushConstants[0] = voxels.transform;
    pushConstants[1] = fpMatrix(glm::ivec3(voxels.size));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.fragment.layout.handle, VK_SHADER_STAGE_ALL, 0, BYTE_SIZE(pushConstants), pushConstants.data());
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, indices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void Voxelization::hybridVoxelization(VkCommandBuffer commandBuffer) {
    triangleParallelVoxelizationIndirect(commandBuffer);
    fragmentParallelVoxelizationIndirect(commandBuffer);
}

void Voxelization::classifyHybridTriangles(VkCommandBuffer commandBuffer) {
    std::array<VkDrawIndexedIndirectCommand, 2> drawCommands{};
    drawCommands[0].instanceCount = 1;
    drawCommands[1].instanceCount = 1;

    vkCmdUpdateBuffer(commandBuffer, model.hybrid.drawCommands, 0, BYTE_SIZE(drawCommands), drawCommands.data());
    vkCmdFillBuffer(commandBuffer, model.hybrid.stats, 0, sizeof(HybridStats), 0);

    std::array<VkBufferMemoryBarrier, 2> transferToCompute{};
    for(auto& barrier : transferToCompute) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }
    transferToCompute[0].buffer = model.hybrid.stats;
    transferToCompute[1].buffer = model.hybrid.drawCommands;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        COUNT(transferToCompute),
        transferToCompute.data(),
        0,
        nullptr);

    HybridClassifierConstants constants{};
    constants.worldToVoxelTransform = voxels.transform;
    constants.cutoffArea = model.hybrid.cutoffArea;
    constants.voxelSize = voxels.size;
    constants.triangleCount = model.hybrid.sourceTriangleCount;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.hybridClassifier.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.hybridClassifier.layout.handle, 0, 1, &model.hybrid.descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, pipelines.hybridClassifier.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HybridClassifierConstants), &constants);
    vkCmdDispatch(commandBuffer, glm::max(1u, (model.hybrid.sourceTriangleCount + 63u) / 64u), 1, 1);

    std::array<VkBufferMemoryBarrier, 4> computeToDraw{};
    for(auto& barrier : computeToDraw) {
        barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.offset = 0;
        barrier.size = VK_WHOLE_SIZE;
    }

    computeToDraw[0].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    computeToDraw[0].buffer = model.hybrid.triangleIndices;
    computeToDraw[1].dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
    computeToDraw[1].buffer = model.hybrid.fragmentIndices;
    computeToDraw[2].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    computeToDraw[2].buffer = model.hybrid.drawCommands;
    computeToDraw[3].dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    computeToDraw[3].buffer = model.hybrid.stats;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        COUNT(computeToDraw),
        computeToDraw.data(),
        0,
        nullptr);

    model.hybrid.dirty = false;
    refreshVoxel = true;
}

void Voxelization::triangleParallelVoxelizationIndirect(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.triangle.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.triangle.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), glm::value_ptr(voxels.transform));
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.hybrid.triangleIndices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexedIndirect(commandBuffer, model.hybrid.drawCommands, 0, 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Voxelization::fragmentParallelVoxelizationIndirect(VkCommandBuffer commandBuffer) {
    static std::array<glm::mat4, 2> pushConstants{};
    VkDeviceSize offset = 0;
    pushConstants[0] = voxels.transform;
    pushConstants[1] = fpMatrix(glm::ivec3(voxels.size));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.fragment.layout.handle, 0, 1, &voxels.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, pipelines.fragment.layout.handle, VK_SHADER_STAGE_ALL, 0, BYTE_SIZE(pushConstants), pushConstants.data());
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, model.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, model.hybrid.fragmentIndices, 0, VK_INDEX_TYPE_UINT32);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdDrawIndexedIndirect(commandBuffer, model.hybrid.drawCommands, sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
}

void Voxelization::generateVoxelTransforms(VkCommandBuffer commandBuffer) {
    glm::uvec3 wg{ glm::max(1u, (voxels.size + 7u)/8u)};


    VkImageMemoryBarrier voxelBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    voxelBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    voxelBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    voxelBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    voxelBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    voxelBarrier.image = voxels.texture.image;
    voxelBarrier.subresourceRange = DEFAULT_SUB_RANGE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, 0, 0, 0, 1, &voxelBarrier);

    VoxelData data{};
    data.worldToVoxelTransform = voxels.transform;
    data.voxelToWordTransform = glm::inverse(voxels.transform);
    data.maxVoxels = static_cast<int>(MaxVoxelTransforms);
    vkCmdUpdateBuffer(commandBuffer, voxels.dataBuffer, 0, sizeof(VoxelData), &data);
    Barrier::transferWriteToComputeRead(commandBuffer, voxels.dataBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.genVoxelTransforms.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines.genVoxelTransforms.layout.handle, 0, 1, &voxels.descriptorSet, 0, 0);
    vkCmdDispatch(commandBuffer, wg.x, wg.y, wg.z);
    Barrier::computeWriteToFragmentRead(commandBuffer, { voxels.transforms });
}

void Voxelization::createVoxelStorage() {

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    voxels.texture.sampler = device.createSampler(samplerInfo);
    textures::create(device, voxels.texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_UINT, Dimension3D<uint32_t>(voxels.size));
    voxels.texture.image.transitionLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);
    updateVoxelDescriptorSet();
}

void Voxelization::updateVoxelDescriptorSet() {
    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = voxels.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{VK_NULL_HANDLE, voxels.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &imageInfo;
    
    writes[1].dstSet = voxels.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo xformInfo{ voxels.transforms, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &xformInfo;

    writes[2].dstSet = voxels.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo dataInfo{ voxels.dataBuffer, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &dataInfo;

    writes[3].dstSet = voxels.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo texInfo{voxels.texture.sampler.handle, voxels.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &texInfo;

    voxels.transformsDescriptorSet = AppContext::allocateInstanceDescriptorSet();

    VkCopyDescriptorSet copySet{ VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET };
    copySet.srcSet = voxels.descriptorSet;
    copySet.srcBinding = 1;
    copySet.srcArrayElement = 0;
    copySet.dstSet = voxels.transformsDescriptorSet;
    copySet.dstBinding = 0;
    copySet.dstArrayElement = 0;
    copySet.descriptorCount = 1;
    
    device.updateDescriptorSets(writes, { copySet });
}

void Voxelization::endFrame() {
}

glm::mat4 Voxelization::fpMatrix(glm::ivec3 voxelDim) {
    float l = 0.0f; //left
    float b = 0.0f; //bottom
    float n = 0.0f; //near

    auto r = (float)voxelDim.x; //right
    auto t = (float)voxelDim.y; //top
    auto f = (float)voxelDim.z; //far

    float inv_dx = 1.0f / (r-l);
    float inv_dy = 1.0f / (t-b);
    float inv_dz = 1.0f / (f-n);

    glm::mat4 matrix{
            2*inv_dx,        0,               0,             0,
            0,               2*inv_dy,        0,             0,
            0,               0,               inv_dz,        0,
            -1*(r+l)*inv_dx, -1*(t+b)*inv_dy, 0,             1
    };

    return matrix;
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1024;
        settings.width = 1024;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
        settings.enabledFeatures.fragmentStoresAndAtomics = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = Voxelization{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
