#include "TerrainMC.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

TerrainMC::TerrainMC(const Settings& settings) : VulkanBaseApp("Marching Cube Terrain", settings) {
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("terrain_marching_cube");
    fileManager().addSearchPathFront("terrain_marching_cube/data");
    fileManager().addSearchPathFront("terrain_marching_cube/spv");
    fileManager().addSearchPathFront("terrain_marching_cube/models");
    fileManager().addSearchPathFront("terrain_marching_cube/textures");
}

void TerrainMC::initApp() {
    checkInvariants();
    initBlockData();
    initVoxels();
    createCube();
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createDescriptorSetLayouts();
    initHelpers();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    generateBlocks();
}

void TerrainMC::checkInvariants() {
    assert(sizeof(CameraInfo) == 336);
}

void TerrainMC::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.zNear = 1.0;
    cameraSettings.zFar = 10;
    cameraSettings.acceleration = glm::vec3(10);
    cameraSettings.velocity = glm::vec3(20);

    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);

//    camera->lookAt(glm::vec3(0, 0, 5), glm::vec3(0), {0, 1, 0});
    cameraSettings.zFar = 1000;
    cameraSettings.fieldOfView = 90.f;

    debugCamera.proj = vkn::ortho(-20, 20, -20, 20, -20, 20);
    debugCamera.view = glm::lookAt({0, 20, 0}, glm::vec3(0), {0, 0, -1});
//    debugCamera.proj = vkn::perspective(glm::radians(90.f), swapChain.aspectRatio(), 1, 1000);
//    debugCamera.view = glm::lookAt({0, 0, 20}, glm::vec3(0), {0, 1, 0});

    auto xform = glm::translate(glm::mat4{1}, {0, 0, 0.5});
    xform = glm::scale(xform, {1, 1, 0.5});
    cBounds = primitives::cubeOutline({0, 1, 1, 1}, xform);

    cameraBounds.vertices = device.createDeviceLocalBuffer(cBounds.vertices.data(), BYTE_SIZE(cBounds.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    gpuData.cameraInfo.resize(MAX_IN_FLIGHT_FRAMES);

    for(auto i = 0; i <MAX_IN_FLIGHT_FRAMES; ++i) {
        gpuData.cameraInfo[i] = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                                            , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(CameraInfo), "camera_info");
    }
}

void TerrainMC::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void TerrainMC::beforeDeviceCreation() {
    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        devFeatures12.scalarBlockLayout = VK_TRUE;
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
}

void TerrainMC::createDescriptorPool() {
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


void TerrainMC::createDescriptorSetLayouts() {
    cameraDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("camera_descriptor_set_layout")
                .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    terrainDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("terrain_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(300)
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
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(300)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(300)
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
}

void TerrainMC::updateDescriptorSets(){
    cameraDescriptorSet.resize(2);
    auto sets = descriptorPool.allocate({ cameraDescriptorSetLayout, cameraDescriptorSetLayout, terrainDescriptorSetLayout });
    cameraDescriptorSet[0] = sets[0];
    cameraDescriptorSet[1] = sets[1];
    terrainDescriptorSet = sets[2];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("camera_descriptor_set_0", cameraDescriptorSet[0]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("camera_descriptor_set_1", cameraDescriptorSet[1]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("terrain_descriptor_set", terrainDescriptorSet);

    auto writes = initializers::writeDescriptorSets<9>();
    
    writes[0].dstSet = cameraDescriptorSet[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo cameraInfo0 { gpuData.cameraInfo[0], 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &cameraInfo0;

    writes[1].dstSet = cameraDescriptorSet[1];
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo cameraInfo1 { gpuData.cameraInfo[1], 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &cameraInfo1;

    writes[2].dstSet = terrainDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = poolSize;
    std::vector<VkDescriptorBufferInfo> vertexInfos;

    vertexInfos.reserve(poolSize);
    for(auto& buffer : gpuData.vertices) {
        vertexInfos.emplace_back(buffer, 0, VK_WHOLE_SIZE);
    }
    writes[2].pBufferInfo = vertexInfos.data();

    writes[3].dstSet = terrainDescriptorSet;
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;

    VkDescriptorBufferInfo blockInfo{ gpuData.blockData, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &blockInfo;
    
    writes[4].dstSet = terrainDescriptorSet;
    writes[4].dstBinding = 2;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo distanceInfo{ gpuData.distanceToCamera, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &distanceInfo;
    
    writes[5].dstSet = terrainDescriptorSet;
    writes[5].dstBinding = 3;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo countersInfo{ gpuData.counters, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &countersInfo;

    writes[6].dstSet = terrainDescriptorSet;
    writes[6].dstBinding = 4;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo blockHashInfo{ gpuData.blockHash, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &blockHashInfo;

    writes[7].dstSet = terrainDescriptorSet;
    writes[7].dstBinding = 5;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = poolSize;

    std::vector<VkDescriptorImageInfo> voxelInfo;
    voxelInfo.reserve(poolSize);

    for(auto& voxel : gpuData.voxels) {
        voxelInfo.push_back({voxel.sampler.handle, voxel.imageView.handle, VK_IMAGE_LAYOUT_GENERAL} );
    }
    writes[7].pImageInfo = voxelInfo.data();

    writes[8].dstSet = terrainDescriptorSet;
    writes[8].dstBinding = 6;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[8].descriptorCount = poolSize;
    writes[8].pImageInfo = voxelInfo.data();

    std::vector<VkCopyDescriptorSet> copySets(2, { .sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET } );

    copySets[0].srcSet = set.descriptorSet();
    copySets[0].srcBinding = 0;
    copySets[0].srcArrayElement = 0;
    copySets[0].dstSet = terrainDescriptorSet;
    copySets[0].dstBinding = 7;
    copySets[0].dstArrayElement = 0;
    copySets[0].descriptorCount = 1;

    copySets[1].srcSet = set.descriptorSet();
    copySets[1].srcBinding = 5;
    copySets[1].srcArrayElement = 0;
    copySets[1].dstSet = terrainDescriptorSet;
    copySets[1].dstBinding = 8;
    copySets[1].dstArrayElement = 0;
    copySets[1].descriptorCount = 1;

    device.updateDescriptorSets(writes, copySets);

}

void TerrainMC::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void TerrainMC::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void TerrainMC::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .inputAssemblyState()
                    .lines()
                .dynamicState()
                    .primitiveTopology()
                .name("render")
                .build(render.layout);

        camRender.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("camera.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .inputAssemblyState()
                    .lines()
                .rasterizationState()
                    .lineWidth(2.5)
                .depthStencilState()
                    .compareOpAlways()
                .dynamicState()
                    .primitiveTopology()
                .name("camera_render")
                .build(camRender.layout);


        blockRender.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("block.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .inputAssemblyState()
                    .lines()
                .layout()
                    .addDescriptorSetLayout(cameraDescriptorSetLayout)
                    .addDescriptorSetLayout(terrainDescriptorSetLayout)
                .name("block_render")
                .build(blockRender.layout);

    //    @formatter:on
}


void TerrainMC::onSwapChainDispose() {
    dispose(render.pipeline);
}

void TerrainMC::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *TerrainMC::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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
    VkDeviceSize offset = 0;

//    for(const auto& transform : visibleList) {
//        renderCube(commandBuffer, debugCamera, transform, true);
//    }
////
//    renderCube(commandBuffer, debugCamera, glm::translate(glm::mat4(1), {0, 0, -1}) * tinyCube);
//    renderCube(commandBuffer, debugCamera, glm::translate(glm::mat4(1), {0, 0, -10}) * tinyCube);

    renderBlocks(commandBuffer);

    renderCamera(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void TerrainMC::renderCamera(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;

    // render frustum
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, camRender.pipeline.handle);
    debugCamera.model = cameraBounds.transform;
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &debugCamera);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cameraBounds.vertices, &offset);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    vkCmdDraw(commandBuffer, cameraBounds.vertices.sizeAs<Vertex>(), 1, 0, 0);

    // render frustum bounding box
//    renderCube(commandBuffer, debugCamera, cameraBounds.aabb, true);

}

void TerrainMC::update(float time) {
    static float angle = 0;
    static float speed = 50;
    angle = time * speed;

//    camera->rotate(angle, 0, 0);
    camera->update(time);
    glfwSetWindowTitle(window, fmt::format("{}, fps - {}", title, framePerSecond).c_str());
    debugConstants.elapsed_time += time;
}

void TerrainMC::checkAppInputs() {
    camera->processInput();
}

void TerrainMC::cleanup() {
    AppContext::shutdown();
}

void TerrainMC::onPause() {
    VulkanBaseApp::onPause();
}

void TerrainMC::createCube() {
    auto mesh = primitives::cube(randomColor(), glm::scale(glm::mat4{1}, glm::vec3(0.5)));

    cube.solid.vertices = device.createDeviceLocalBuffer(mesh.vertices.data(), BYTE_SIZE(mesh.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cube.solid.indexes = device.createDeviceLocalBuffer(mesh.indices.data(), BYTE_SIZE(mesh.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    auto vertices = primitives::cubeOutline({1, 1, 0, 1}, glm::scale(glm::mat4{1}, glm::vec3(0.5)));
    cube.outline.vertices = device.createDeviceLocalBuffer(vertices.vertices.data(), BYTE_SIZE(vertices.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    tinyCube = glm::scale(glm::mat4(1), glm::vec3(0.25));

}

void TerrainMC::updateVisibilityList() {
    visibleList.clear();
    cube.instances.clear();
    skipList.clear();

//    static auto bd = gpuData.blockData.span<BlockData>(poolSize * 10);
//    static auto cs = reinterpret_cast<Counters*>(gpuData.counters.map());
//    auto counters = *cs;
//    static std::vector<BlockData> blockData;
//    blockData.clear();
//    blockData.insert(blockData.end(), bd.begin(), bd.end());
//
//    for(auto i = 0; i < counters.block_id; ++i) {
//        auto center = blockData[i].aabb;
//        auto transform = translate(glm::mat4{1}, center);
//        visibleList.push_back(transform);
//    }


    auto dim = glm::ivec3(cameraInfo.aabbMax - cameraInfo.aabbMin) + 2;

    auto n = glm::vec3(dim - 1);
    cube.instances.reserve(dim.z * dim.y * dim.x);
    for(auto z = 0; z < dim.z; ++z) {
        for (auto y = 0; y < dim.y; ++y) {
            for (auto x = 0; x < dim.x; ++x) {
                glm::vec3 position =  glm::vec3(x, y, z);
                position = remap(position, glm::vec3(0), n, -n * 0.5f, n * 0.5f);
                auto transform = cameraInfo.grid_to_world * glm::translate(glm::mat4(1), position);
                cube.instances.push_back(transform);
            }
        }
    }

    visibleList.clear();
    skipList.clear();

    static Frustum frustum{};
    camera->extract(frustum);

    for(const auto & transform : cube.instances) {

        const auto center = transform * glm::vec4(0, 0, 0, 1);

        if(frustum.test(center, 1)) {
            visibleList.push_back(transform);
        }else {
            skipList.push_back(transform);
        }
    }
}

void TerrainMC::newFrame() {
    cameraBounds.transform = glm::inverse(camera->camera.proj * camera->camera.view);
    static auto identity = glm::mat4{1};
    debugCamera.model = identity;

    const auto near = camera->near();
    const auto far = camera->far();
    const auto camPosOffset = glm::vec3(0, 0, -(far - near) * 0.5 - near);


    const auto cam = camera->cam();
    cameraInfo.position = camera->position();
    cameraInfo.view_projection = cam.proj * cam.view;
    cameraInfo.inverse_view_projection = glm::inverse(cam.proj * cam.view);
    cameraInfo.grid_to_world = glm::inverse(camera->cam().view) * glm::translate(glm::mat4(1), camPosOffset);;
    camera->extract(cameraInfo.frustum);
    computeCameraBounds();
    debugScene();
//    generateTerrain();
//    updateVisibilityList();
}

void TerrainMC::computeCameraBounds() {
    camera->extractAABB(cameraInfo.aabbMin, cameraInfo.aabbMax);

    auto dim = glm::ceil(cameraInfo.aabbMin - cameraInfo.aabbMax);

    cameraBounds.aabb = glm::mat4{1};
    cameraBounds.aabb = glm::translate(cameraBounds.aabb, {0, 0, -dim.z * 0.5 - camera->znear});
    cameraBounds.aabb = glm::scale(cameraBounds.aabb, dim);
    cameraBounds.aabb = glm::inverse(camera->camera.view) * cameraBounds.aabb;
}

void TerrainMC::renderCube(VkCommandBuffer commandBuffer, Camera& aCamera, const glm::mat4 &model, bool outline) {
    VkDeviceSize offset = 0;
    if(outline) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        aCamera.model = model;
        vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &aCamera);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.outline.vertices, &offset);

        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        vkCmdDraw(commandBuffer, cube.outline.vertices.sizeAs<Vertex>(), 1, 0, 0);
    }else {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        aCamera.model = model;
        vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &aCamera);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.solid.vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, cube.solid.indexes, 0, VK_INDEX_TYPE_UINT32);

        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
        vkCmdDrawIndexed(commandBuffer, cube.solid.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);
    }
}

void TerrainMC::initBlockData() {
    glm::uvec3 blockRes{32};
    const auto numSidesOnTriangle = 3;
    const auto numTrianglesPerCell = 5 * numSidesOnTriangle;
    const auto percentageOfBlock = 0.6;
    VkDeviceSize size = blockRes.x * blockRes.y * blockRes.z * sizeof(BlockVertex) * numTrianglesPerCell;
    size *= percentageOfBlock;

    VkBufferUsageFlags transferReadWrite = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    gpuData.vertices.resize(poolSize);
    for(auto i = 0; i < poolSize; ++i) {
        gpuData.vertices[i] = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY, size, fmt::format("block_vertex_{}", i));
    }
    const auto debugDim = debugConstants.dim;
    const auto numBlocks =  debugDim.x * debugDim.y * debugDim.z;
    size = sizeof(BlockData) * numBlocks;
    gpuData.blockData = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_ONLY, size, "block_data");

//    size = sizeof(float) * poolSize;
    std::vector<uint32_t> allocation(poolSize, 0xffffffff);
    gpuData.distanceToCamera = device.createDeviceLocalBuffer(allocation.data(), BYTE_SIZE(allocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite);
    device.setName<VK_OBJECT_TYPE_BUFFER>("distance_to_camera", gpuData.distanceToCamera.buffer);

    size = sizeof(Counters);
    gpuData.counters = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, size, "atomic_counters");
    counters = reinterpret_cast<Counters*>(gpuData.counters.map());

    size = sizeof(uint32_t) * numBlocks;
    gpuData.blockHash = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_TO_CPU, size, "block_hash");
}

void TerrainMC::initVoxels() {
    glm::uvec3 dim{32};

    gpuData.voxels.resize(poolSize);
    for(auto i = 0; i < poolSize; ++i) {
        textures::create(device, gpuData.voxels[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));
    }
}

void TerrainMC::generateTerrain() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        vkCmdFillBuffer(commandBuffer, gpuData.counters, 0, gpuData.counters.size, 0);
        counters->set_add_id = 0;
        vkCmdFillBuffer(commandBuffer, gpuData.blockHash, 0, gpuData.blockHash.size, ~0u);
        vkCmdUpdateBuffer(commandBuffer, gpuData.cameraInfo[0], 0, sizeof(CameraInfo), &cameraInfo);

        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        dependencyInfo.memoryBarrierCount = 1;
        dependencyInfo.pMemoryBarriers = &memoryBarrier;


        static Records blockRecords {
                .buffer = gpuData.blockData,
                .size = sizeof(BlockData),
        };

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
        sort(commandBuffer, gpuData.distanceToCamera, blockRecords);

        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        generateBlocks(commandBuffer);

        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        set.insert(commandBuffer, gpuData.blockHash.region(0));
        computeDistanceToCamera(commandBuffer);

        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    });
}

void TerrainMC::debugScene() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        vkCmdFillBuffer(commandBuffer, gpuData.blockHash, 0, gpuData.blockHash.size, ~0u);
        vkCmdUpdateBuffer(commandBuffer, gpuData.cameraInfo[0], 0, sizeof(CameraInfo), &cameraInfo);

        transferToComputeBarrier(commandBuffer);

        blockInCameraTest(commandBuffer);
        computeToComputeBarrier(commandBuffer);

        set.insert(commandBuffer, gpuData.blockHash.region(0));
        computeDistanceToCamera(commandBuffer);

//        computeToComputeBarrier(commandBuffer);
//        sortBlocks(commandBuffer);

        computeToRenderBarrier(commandBuffer);
    });
}

void TerrainMC::generateBlocks(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = cameraDescriptorSet[0];
    sets[1] = terrainDescriptorSet;

    auto gc = (glm::uvec3(cameraInfo.aabbMax - cameraInfo.aabbMin) + 2u);
    gc = gc/8u + 1u;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("generate_blocks"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("generate_blocks"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
}

void TerrainMC::generateBlocks() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        const auto debugDim = debugConstants.dim;
        auto gc = (debugDim)/8;
        gc += glm::sign(debugDim - (gc * 8));

        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = cameraDescriptorSet[0];
        sets[1] = terrainDescriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("generate_debug_blocks"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("generate_debug_blocks"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, compute.layout("generate_debug_blocks"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugConstants), &debugConstants);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);

        memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

        vkCmdFillBuffer(commandBuffer, gpuData.blockHash, 0, gpuData.blockHash.size, ~0u);
    });
}

void TerrainMC::computeDistanceToCamera(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = cameraDescriptorSet[0];
    sets[1] = terrainDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("compute_distance_to_camera"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("compute_distance_to_camera"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, poolSize/128 + 128, 1, 1);
}

void TerrainMC::renderBlocks(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = cameraDescriptorSet[0];
    sets[1] = terrainDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blockRender.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blockRender.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, blockRender.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &debugCamera);
//    camera->push(commandBuffer, blockRender.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.outline.vertices, &offset);

    const auto debugDim = debugConstants.dim;
    uint32_t numBlocks = debugDim.x * debugDim.y * debugDim.z;
    vkCmdDraw(commandBuffer, cube.outline.vertices.sizeAs<Vertex>(), numBlocks, 0, 0);
}

void TerrainMC::initHelpers() {
    compute = TerrainCompute{ device, { &cameraDescriptorSetLayout, &terrainDescriptorSetLayout} };
    compute.init();
    sort = RadixSort{ &device };
    sort.init();
    sort.enableOrderChecking();

    const auto dm = debugConstants.dim;
    const auto tableSize = dm.x * dm.y * dm.z * 2u;
    set = gpu::HashSet{device, descriptorPool, tableSize};
    set.init();
}

void TerrainMC::blockInCameraTest(VkCommandBuffer commandBuffer) {
    const auto debugDim = debugConstants.dim;
    auto gc = (debugDim)/8;
    gc += glm::sign(debugDim - (gc * 8));

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = cameraDescriptorSet[0];
    sets[1] = terrainDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("block_in_frustum_test"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("block_in_frustum_test"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.layout("block_in_frustum_test"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(debugConstants), &debugConstants);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);

}

void TerrainMC::computeToComputeBarrier(VkCommandBuffer commandBuffer) {
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void TerrainMC::computeToRenderBarrier(VkCommandBuffer commandBuffer) {
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void TerrainMC::transferToComputeBarrier(VkCommandBuffer commandBuffer) {
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void TerrainMC::sortBlocks(VkCommandBuffer commandBuffer) {
    static Records blockRecords {
        .buffer = gpuData.blockData,
        .size = sizeof(BlockData),
    };

    sort(commandBuffer, gpuData.distanceToCamera, blockRecords);
}

/************************************** TerrainCompute ***********************************************************/

TerrainCompute::TerrainCompute(VulkanDevice &device, std::vector<VulkanDescriptorSetLayout*> descriptorSetLayouts)
: ComputePipelines(&device)
, descriptorSetLayouts_(std::move(descriptorSetLayouts))
{}

void TerrainCompute::init() {
    createPipelines();
}

std::vector<PipelineMetaData> TerrainCompute::pipelineMetaData() {
    return {
        {
            .name = "generate_blocks",
            .shadePath = FileManager::resource("generate_blocks.comp.spv"),
            .layouts = descriptorSetLayouts_
        },
        {
            .name = "compute_distance_to_camera",
            .shadePath = FileManager::resource("compute_distance_to_camera.comp.spv"),
            .layouts = descriptorSetLayouts_
        },
        {
            .name = "generate_debug_blocks",
            .shadePath = FileManager::resource("generate_debug_blocks.comp.spv"),
            .layouts = descriptorSetLayouts_,
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugConstants)} }
        },
        {
            .name = "block_in_frustum_test",
            .shadePath = FileManager::resource("block_in_frustum_test.comp.spv"),
            .layouts = descriptorSetLayouts_,
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DebugConstants)} }
        },
    };
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 720;
        settings.height = 720;
        settings.depthTest = true;
        settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;
        settings.enabledFeatures.geometryShader = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = TerrainMC{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}