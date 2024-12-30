#include "TerrainMC.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "MarchingCubeLuts.hpp"

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
    initDebugStuff();
    initCamera();
    initBlockData();
    initLuts();
    initVoxels();
    createCube();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createDescriptorSetLayouts();
    initHelpers();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
//    generateTerrain();
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
    camera->lookAt({0, 0, 5}, glm::vec3(0), {0, 1, 0});

//    camera->lookAt(glm::vec3(0, 0, 5), glm::vec3(0), {0, 1, 0});
    cameraSettings.zFar = 1000;
    cameraSettings.fieldOfView = 90.f;

//    debugCamera.proj = vkn::ortho(-20, 20, -20, 20, -20, 20);
//    debugCamera.view = glm::lookAt({0, 20, 0}, glm::vec3(0), {0, 0, -1});
    debugCamera.proj = vkn::perspective(glm::radians(90.f), swapChain.aspectRatio(), 1, 1000);
    debugCamera.view = glm::lookAt({0, 0, 20}, glm::vec3(0), {0, 1, 0});

    auto xform = glm::translate(glm::mat4{1}, {0, 0, 0.5});
    xform = glm::scale(xform, {1, 1, 0.5});
    cBounds = primitives::cubeOutline({0, 1, 1, 1}, xform);

    cameraBounds.vertices = device.createDeviceLocalBuffer(cBounds.vertices.data(), BYTE_SIZE(cBounds.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

//    gpuData.cameraInfo.resize(MAX_IN_FLIGHT_FRAMES);
    gpuData.cameraInfo.resize(2);
    for(auto i = 0; i <2; ++i) {
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

    indirectDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("indirect_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    marchingCubeLutSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("marching_cube_lut_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    terrainDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("terrain_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(poolSize)
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
                .descriptorCount(scratchTextureCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(scratchTextureCount)
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
    auto sets = descriptorPool.allocate({ cameraDescriptorSetLayout, cameraDescriptorSetLayout, 
                                          terrainDescriptorSetLayout, indirectDescriptorSetLayout,
                                          marchingCubeLutSetLayout});
    cameraDescriptorSet[0] = sets[0];
    cameraDescriptorSet[1] = sets[1];
    terrainDescriptorSet = sets[2];
    indirectDescriptorSet = sets[3];
    marchingCubeLutSet = sets[4];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("camera_descriptor_set_0", cameraDescriptorSet[0]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("camera_descriptor_set_1", cameraDescriptorSet[1]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("terrain_descriptor_set", terrainDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("indirect_descriptor_set", indirectDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("marching_cube_lut_descriptor_set", marchingCubeLutSet);

    auto writes = initializers::writeDescriptorSets<13>();
    
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

    std::vector<VkDescriptorImageInfo> voxelInfo;
    voxelInfo.reserve(scratchTextureCount);

    for(auto& voxel : gpuData.voxels) {
        voxelInfo.push_back({voxel.sampler.handle, voxel.imageView.handle, VK_IMAGE_LAYOUT_GENERAL} );
    }

    writes[7].dstSet = terrainDescriptorSet;
    writes[7].dstBinding = 5;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = poolSize;
    writes[7].descriptorCount = voxelInfo.size();
    writes[7].pImageInfo = voxelInfo.data();

    writes[8].dstSet = terrainDescriptorSet;
    writes[8].dstBinding = 6;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[8].descriptorCount = voxelInfo.size();
    writes[8].pImageInfo = voxelInfo.data();

    writes[9].dstSet = indirectDescriptorSet;
    writes[9].dstBinding = 0;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    VkDescriptorBufferInfo dispatchInfo{ gpuData.dispatchIndirectBuffer, 0, VK_WHOLE_SIZE };
    writes[9].pBufferInfo = &dispatchInfo;

    writes[10].dstSet = indirectDescriptorSet;
    writes[10].dstBinding = 1;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo drawInfo{ gpuData.drawIndirectBuffer, 0, VK_WHOLE_SIZE };
    writes[10].pBufferInfo = &drawInfo;

    writes[11].dstSet = marchingCubeLutSet;
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    VkDescriptorBufferInfo edgeLutInfo{ gpuData.edgeLUT, 0, VK_WHOLE_SIZE };
    writes[11].pBufferInfo = &edgeLutInfo;

    writes[12].dstSet = marchingCubeLutSet;
    writes[12].dstBinding = 1;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    VkDescriptorBufferInfo triangleLutInfo{ gpuData.triangleLUT, 0, VK_WHOLE_SIZE };
    writes[12].pBufferInfo = &triangleLutInfo;

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

    gen_sets[0] = cameraDescriptorSet[0];
    gen_sets[1] = terrainDescriptorSet;
    gen_sets[2] = indirectDescriptorSet;
    gen_sets[3] = marchingCubeLutSet;

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
    cubeRender.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("flat.vert.spv"))
                .fragmentShader(resource("flat.frag.spv"))
            .dynamicState()
                .primitiveTopology()
            .name("render")
        .build(cubeRender.layout);

        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(BlockVertex), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(BlockVertex, position))
                    .addVertexAttributeDescription(1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(BlockVertex, normal))
                    .addVertexAttributeDescription(2, 0, VK_FORMAT_R32_SFLOAT, offsetof(BlockVertex, ambient_occlusion))
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .inputAssemblyState()
                    .triangles()
                .rasterizationState()
                    .polygonModeLine()
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
                    .addDescriptorSetLayout(indirectDescriptorSetLayout)
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

//    for(auto& transform : cube.instances) {
//        renderCube(commandBuffer, camera->camera, transform, true);
//    }

    renderScene(commandBuffer);
//    renderBlocks(commandBuffer);
//    renderCamera(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);


    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void TerrainMC::renderScene(VkCommandBuffer commandBuffer) {
    const int numBuffers = counters->slots_used;
    const VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout);

    for(auto i = 0; i < numBuffers; ++i) {
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, gpuData.vertices[i], &offset);
        vkCmdDrawIndirect(commandBuffer, gpuData.drawIndirectBuffer, sizeof(DrawCommand) * i, 1, sizeof(DrawCommand));
    }
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
    cameraInfo.direction = camera->viewDir;
    camera->extract(cameraInfo.frustum);
    computeCameraBounds();
//    debugScene();

    static bool once = true;
    if(once) {
        glm::vec4 world_pos = cameraInfo.grid_to_world * glm::vec4(2.0000, -1.0000, 3.0000, 1);
        fmt::print("world_pos: {}", world_pos.xyz());
        generateTerrain();
        once = false;
    }
//    counters->free_slots = std::max(0u, poolSize - counters->slots_used);
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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeRender.pipeline.handle);
        aCamera.model = model;
        vkCmdPushConstants(commandBuffer, cubeRender.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &aCamera);
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.outline.vertices, &offset);

        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        vkCmdDraw(commandBuffer, cube.outline.vertices.sizeAs<Vertex>(), 1, 0, 0);
    }else {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, cubeRender.pipeline.handle);
        aCamera.model = model;
        vkCmdPushConstants(commandBuffer, cubeRender.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &aCamera);
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

    VkBufferUsageFlags transferReadWrite = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    gpuData.vertices.resize(poolSize);
    for(auto i = 0; i < poolSize; ++i) {
        gpuData.vertices[i] = device.createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           VMA_MEMORY_USAGE_GPU_ONLY, size, fmt::format("block_vertex_{}", i));
    }
    glm::vec3 cMin, cMax;
    camera->extractAABB(cMin, cMax);
    glm::uvec3 cDim = glm::uvec3(cMax - cMin) + 2u;
    auto numBlocks = cDim.x * cDim.y * cDim.z;
    size = sizeof(BlockData) * numBlocks;
    std::vector<BlockData> blockDataAlloc(numBlocks);
//    blockDataAlloc[0] = { .aabb = { 0.0000, 0.0000, 0.0000} };
//    blockDataAlloc[0] = { .aabb = { -2.0000, 0.0000, 0.0000} };
//    blockDataAlloc[1] = { .aabb = { -3.0000, 0.0000, 0.0000} };
//    blockDataAlloc[2] = { .aabb = { 3.0000, 0.0000, 0.0000} };
//    blockDataAlloc[3] = { .aabb = { 2.0000, 0.0000, 0.0000} };
//    blockDataAlloc[4] = { .aabb = { -1.0000, 0.0000, -1.0000} };
//    blockDataAlloc[5] = { .aabb = { 0.0000, 0.0000, -1.0000} };
//    blockDataAlloc[6] = { .aabb = { 2.0000, 0.0000, -1.0000} };
//    blockDataAlloc[7] = { .aabb = { -2.0000, 0.0000, -1.0000} };
//    blockDataAlloc[8] = { .aabb = { 1.0000, 0.0000, -1.0000} };

    gpuData.blockData = device.createDeviceLocalBuffer(blockDataAlloc.data(), BYTE_SIZE(blockDataAlloc), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite);
    device.setName<VK_OBJECT_TYPE_BUFFER>("block_data", gpuData.blockData.buffer);
//    gpuData.blockData = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_ONLY, size, "block_data");

    std::vector<uint32_t> allocation(numBlocks, 0xffffffff);
    gpuData.distanceToCamera = device.createDeviceLocalBuffer(allocation.data(), BYTE_SIZE(allocation), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite);
    device.setName<VK_OBJECT_TYPE_BUFFER>("distance_to_camera", gpuData.distanceToCamera.buffer);

    size = sizeof(Counters);
    gpuData.counters = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size, "atomic_counters");
    counters = reinterpret_cast<Counters*>(gpuData.counters.map());
    counters->free_slots = poolSize;
    counters->slots_used = 0;
//    counters->blocks = 9;

    size = sizeof(uint32_t) * (1 << 20);
    gpuData.blockHash = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_ONLY, size, "block_hash");

    size = sizeof(VkDispatchIndirectCommand);
    gpuData.dispatchIndirectBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_ONLY, size, "dispatch_indirect");

    size = sizeof(DrawCommand) * (poolSize + 1);
    debugDrawOffset = sizeof(DrawCommand) * poolSize;
    gpuData.drawIndirectBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | transferReadWrite, VMA_MEMORY_USAGE_GPU_ONLY, size, "draw_indirect");
    cpuBuffer = device.createStagingBuffer((10 << 20));

    drawCmds = cpuBuffer.span<DrawCommand>(poolSize/10);
}

void TerrainMC::initVoxels() {

    imageMemoryBarrier.srcStageMask = VK_PIPELINE_STAGE_NONE;
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_NONE;
    imageMemoryBarrier.dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageMemoryBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    glm::uvec3 dim{33};
    gpuData.voxels.resize(scratchTextureCount);
    std::vector<VkImageMemoryBarrier2> barriers;
    for(auto i = 0; i < scratchTextureCount; ++i) {
        textures::createNoTransition(device, gpuData.voxels[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dim, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER);

        imageMemoryBarrier.image = gpuData.voxels[i].image.image;
        barriers.push_back(imageMemoryBarrier);
    }

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        dependencyInfo.imageMemoryBarrierCount = COUNT(barriers);
        dependencyInfo.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    });
    dependencyInfo.imageMemoryBarrierCount = 0;

}

void TerrainMC::generateTerrain() {
    device.computeCommandPool().oneTimeCommand([&](auto commandBuffer){
        generateTerrain(commandBuffer);
    });
//    device.copy(gpuData.drawIndirectBuffer, cpuBuffer, sizeof(DrawCommand) * (poolSize/10));
}

void TerrainMC::generateTerrain(VkCommandBuffer commandBuffer) {
    prepareBuffers(commandBuffer);
    generateBlocks(commandBuffer);

    sortBlocks(commandBuffer);
    generateVoxels(commandBuffer);

    set.insert(commandBuffer, gpuData.blockHash.region(0));
//        copyBuffersToCpu(commandBuffer);
    computeDrawBlocks(commandBuffer);
    computeToRenderBarrier(commandBuffer);
}

void TerrainMC::prepareBuffers(VkCommandBuffer commandBuffer) {
    static VkDispatchIndirectCommand dispatchCmd{0, 1, 1};
    static DrawCommand drawCmd{24, 0, 0, 0};

//    counters->blocks = 0;
    counters->set_add_id = 0;
//    vkCmdUpdateBuffer(commandBuffer, gpuData.counters, 0, sizeof(Counters), &counters);
    vkCmdFillBuffer(commandBuffer, gpuData.distanceToCamera, 0, gpuData.distanceToCamera.size, ~0u);
    vkCmdFillBuffer(commandBuffer, gpuData.blockHash, 0, gpuData.blockHash.size, ~0u);
    vkCmdUpdateBuffer(commandBuffer, gpuData.cameraInfo[0], 0, sizeof(CameraInfo), &cameraInfo);
    vkCmdUpdateBuffer(commandBuffer, gpuData.dispatchIndirectBuffer, 0, sizeof(VkDispatchIndirectCommand), &dispatchCmd);
    vkCmdUpdateBuffer(commandBuffer, gpuData.drawIndirectBuffer, debugDrawOffset, sizeof(DrawCommand), &drawCmd);
    transferToComputeBarrier(commandBuffer);
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
    auto dim = glm::ivec3(cameraInfo.aabbMax - cameraInfo.aabbMin) + 2;
    auto gc = dim/8 + glm::sign(dim - (dim/8 * 8));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("generate_blocks"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("generate_blocks"), 0, COUNT(gen_sets), gen_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    computeToComputeBarrier(commandBuffer);
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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blockRender.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blockRender.layout.handle, 0, 3, gen_sets.data(), 0, VK_NULL_HANDLE);
//    vkCmdPushConstants(commandBuffer, blockRender.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &debugCamera);
    camera->push(commandBuffer, blockRender.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.outline.vertices, &offset);

//    vkCmdDrawIndirect(commandBuffer, gpuData.drawIndirectBuffer, debugDrawOffset, 1, sizeof(DrawCommand));
    vkCmdDraw(commandBuffer, cube.outline.vertices.sizeAs<Vertex>(), counters->slots_used, 0, 0);
}

void TerrainMC::initHelpers() {
    compute = TerrainCompute{ device, {
        &cameraDescriptorSetLayout, &terrainDescriptorSetLayout,
        &indirectDescriptorSetLayout, &marchingCubeLutSetLayout} };
    compute.init();
    sort = RadixSort{ &device };
    sort.init();
    sort.enableOrderChecking();

    const uint32_t tableSize = gpuData.blockHash.size * 2;
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


void TerrainMC::computeToTransferBarrier(VkCommandBuffer commandBuffer) {
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;

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
    computeToComputeBarrier(commandBuffer);
}

void TerrainMC::computeDrawBlocks(VkCommandBuffer commandBuffer) {
    auto gc = set.capacity()/1024;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("compute_blocks_to_draw"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("compute_blocks_to_draw"), 0, COUNT(gen_sets), gen_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc, 1, 1);
}


void TerrainMC::generateTextures(VkCommandBuffer commandBuffer, int pass) {
    const auto gc = scratchTextureCount;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("generate_textures"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("generate_textures"), 0, COUNT(gen_sets), gen_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.layout("generate_textures"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenConstants), &voxelGenConstants);
    vkCmdDispatch(commandBuffer, 1, gc, 1);
}

void TerrainMC::generateVoxels(VkCommandBuffer commandBuffer) {
    const int numBlocks = gpuData.blockData.sizeAs<BlockData>();
    const int passes = numBlocks/scratchTextureCount + glm::sign(numBlocks - (numBlocks/scratchTextureCount) * scratchTextureCount);
//    const int passes = 2;
    voxelGenConstants.blocksPerPass = scratchTextureCount;
    for(auto pass = 0; pass < passes; ++pass) {
        voxelGenConstants.pass = pass;
        generateTextures(commandBuffer, pass);
        computeToComputeBarrier(commandBuffer);
        marchTextures(commandBuffer, pass);
        computeReadToComputeWriteBarrier(commandBuffer);
    }
    computeToComputeBarrier(commandBuffer);
}

void TerrainMC::marchTextures(VkCommandBuffer commandBuffer, int pass) {
    const auto gc = scratchTextureCount;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("march_voxels"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("march_voxels"), 0, COUNT(gen_sets), gen_sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.layout("march_voxels"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenConstants), &voxelGenConstants);
    vkCmdDispatch(commandBuffer, 1, gc, 1);
}


void TerrainMC::initLuts() {
    gpuData.edgeLUT = device.createDeviceLocalBuffer(EdgeTable.data(), BYTE_SIZE(EdgeTable), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    gpuData.triangleLUT = device.createDeviceLocalBuffer(TriangleTable.data(), BYTE_SIZE(TriangleTable), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
}

void TerrainMC::copyBuffersToCpu(VkCommandBuffer commandBuffer) {
    computeToTransferBarrier(commandBuffer);
    VkBufferCopy copy{};
    copy.size = sizeof(DrawCommand) * (poolSize/10);;
    copy.srcOffset = 0;
    copy.dstOffset = 0;
    vkCmdCopyBuffer(commandBuffer, gpuData.drawIndirectBuffer, cpuBuffer, 1u, &copy);
}

void TerrainMC::initDebugStuff() {
    cube.instances.push_back(glm::translate(glm::mat4{1}, glm::vec3(2.0000, -1.0000, 1.0000)));
    cube.instances.push_back(glm::translate(glm::mat4{1}, glm::vec3(1.0000, -1.0000, 4.0000)));
    cube.instances.push_back(glm::translate(glm::mat4{1}, glm::vec3(0.0000, -1.0000, 3.0000)));
}

void TerrainMC::computeReadToComputeWriteBarrier(VkCommandBuffer commandBuffer) {
    memoryBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
    memoryBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
    dependencyInfo.memoryBarrierCount = 1;
    dependencyInfo.pMemoryBarriers = &memoryBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
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
            .name = "compute_blocks_to_draw",
            .shadePath = FileManager::resource("compute_blocks_to_draw.comp.spv"),
            .layouts = descriptorSetLayouts_
        },
        {
            .name = "generate_textures",
            .shadePath = FileManager::resource("generate_textures.comp.spv"),
            .layouts = descriptorSetLayouts_,
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenConstants) }}
        },
        {
            .name = "march_voxels",
            .shadePath = FileManager::resource("march_voxels.comp.spv"),
            .layouts = descriptorSetLayouts_,
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VoxelGenConstants) }}
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
        settings.queueFlags |= VK_QUEUE_COMPUTE_BIT;
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