#include "Voronoi.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

Voronoi::Voronoi(const Settings& settings) : VulkanBaseApp("voronoi", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/voronoi");
    fileManager.addSearchPathFront("../../examples/voronoi/data");
    fileManager.addSearchPathFront("../../examples/voronoi/spv");
    fileManager.addSearchPathFront("../../examples/voronoi/models");
    fileManager.addSearchPathFront("../../examples/voronoi/textures");

    static VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    deviceCreateNextChain = &dynamicRenderingFeatures;
}

void Voronoi::initApp() {
    initCamera();
    initClipSpaceBuffer();
    initGenerators();
    initPrefixSum();
    createGBuffer();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    runGeneratePoints();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void Voronoi::initCamera() {
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

void Voronoi::createGBuffer() {
    VkImageCreateInfo imageSpec = initializers::imageCreateInfo(
            VK_IMAGE_TYPE_2D,
            VK_FORMAT_D16_UNORM,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            width,
            height);

    VkImageSubresourceRange subresourceRange = initializers::imageSubresourceRange(VK_IMAGE_ASPECT_DEPTH_BIT);
    gBuffer.depth.image = device.createImage(imageSpec);
    gBuffer.depth.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

    gBuffer.depth.imageView = gBuffer.depth.image.createView(imageSpec.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    gBuffer.depth.sampler = device.createSampler(samplerInfo);

    imageSpec.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageSpec.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    gBuffer.color.image = device.createImage(imageSpec);
    gBuffer.color.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
    gBuffer.color.imageView = gBuffer.color.image.createView(imageSpec.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);
    gBuffer.color.sampler = device.createSampler(samplerInfo);
    gBuffer.color.width = width;
    gBuffer.color.height = height;
}

void Voronoi::initClipSpaceBuffer() {
    auto vertexAndUv = ClipSpace::Quad::positions;
    std::vector<glm::vec2> vertices;

    for(int i = 0; i < vertexAndUv.size(); i+= 2) {
        vertices.push_back(vertexAndUv[i]);
    }
    clipVertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void Voronoi::runGeneratePoints() {
    Texture texture;
    textures::fromFile(device, texture, R"(C:\Users\Josiah Ebhomenye\Downloads\1453849.jpg)");
    VulkanDescriptorSetLayout setLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
    
    auto texDescriptorSet = descriptorPool.allocate({ setLayout }).front();
    auto writes = initializers::writeDescriptorSets();
    writes[0].dstSet = texDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo info{ texture.sampler.handle, texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &info;

    device.updateDescriptorSets(writes);

    auto module = device.createShaderModule(resource("generate_points.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    // Generator points
    VulkanPipelineLayout layout = device.createPipelineLayout( { setLayout, descriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = layout.handle;

    VulkanPipeline pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = texDescriptorSet;
        sets[1] = descriptorSet;

        vkCmdBindPipeline(commandBuffer , VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdDispatch(commandBuffer, numGenerators, 1, 1);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

}

void Voronoi::initGenerators() {
    constants.screenWidth = width;
    constants.screenHeight = height;
    auto imageSize = width * height;

    std::vector<glm::vec2> points(numGenerators);

    auto hash31 = [](float p){
        using namespace glm;
        vec3 p3 = fract(vec3(p) * vec3(.1031, .1030, .0973));
        p3 += dot(p3, p3.yzx()+33.33f);
        return fract((p3.xxy+p3.yzz)*p3.zyx);
    };

//    auto rngNum = rng(-1, 1, 1 << 20);
//    for(int i = 0; i < numGenerators; i++){
//        points.emplace_back(rngNum(), rngNum());
//    }

    generators = device.createDeviceLocalBuffer(points.data(), BYTE_SIZE(points), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    std::vector<glm::vec2> centers(numGenerators);
    centroids = device.createDeviceLocalBuffer(centers.data(), BYTE_SIZE(centers), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);


    regions = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                    , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec4)  * imageSize, "region_gen_map");
    regionReordered = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                                    , VMA_MEMORY_USAGE_GPU_ONLY, sizeof(glm::vec4)  * imageSize, "region_reorder_buffer");

    std::vector<int> count(numGenerators);
    counts = device.createDeviceLocalBuffer(count.data(), BYTE_SIZE(count)
                                            , VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                                            | VK_BUFFER_USAGE_TRANSFER_DST_BIT
                                            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    std::vector<glm::vec4> cols;
    std::map<int, std::vector<glm::vec4>>  colorHash;

    auto rngColor = rng(0, 1, 1 << 20);
    for(int i = 0; i < numGenerators; i++){
//        glm::vec3 color{ rngColor(), rngColor(), rngColor()};
        glm::vec3 color = hash31(float(i+1));
        cols.emplace_back(color, i);
    }

    colors = device.createDeviceLocalBuffer(cols.data(), BYTE_SIZE(cols), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    auto aCone = primitives::cone(100, 100, 1.0, 1.0, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    cone.vertices = device.createDeviceLocalBuffer(aCone.vertices.data(), BYTE_SIZE(aCone.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cone.indices = device.createDeviceLocalBuffer(aCone.indices.data(), BYTE_SIZE(aCone.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}


void Voronoi::createDescriptorPool() {
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

void Voronoi::initPrefixSum() {
    prefixSum = PrefixSum{ &device };
    prefixSum.init();
    prefixSum.updateDataDescriptorSets(counts);
}

void Voronoi::createDescriptorSetLayouts() {
    descriptorSetLayout =
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
            .createLayout();
    
    voronoiRegionsSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("voronoi_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
            
}

void Voronoi::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { descriptorSetLayout, voronoiRegionsSetLayout });
    descriptorSet = sets[0];
    voronoiDescriptorSet = sets[1];
    
    auto writes = initializers::writeDescriptorSets<9>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo generatorInfo{ generators, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &generatorInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo colorInfo{ colors, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &colorInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo regionsInfo{ regions, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &regionsInfo;

    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo countsInfo{ counts, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &countsInfo;

    writes[4].dstSet = descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo centroidsInfo{ centroids, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &centroidsInfo;

    writes[5].dstSet = descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo regionReorderInfo{ regionReordered, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &regionReorderInfo;

    writes[6].dstSet = voronoiDescriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo colorImageInfo{gBuffer.color.sampler.handle, gBuffer.color.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[6].pImageInfo = &colorImageInfo;

    writes[7].dstSet = voronoiDescriptorSet;
    writes[7].dstBinding = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[7].descriptorCount = 1;
    VkDescriptorImageInfo depthImageInfo{gBuffer.depth.sampler.handle, gBuffer.depth.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[7].pImageInfo = &depthImageInfo;

    writes[8].dstSet = voronoiDescriptorSet;
    writes[8].dstBinding = 2;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[8].descriptorCount = 1;
    VkDescriptorImageInfo colorImageInfo1{VK_NULL_HANDLE, gBuffer.color.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[8].pImageInfo = &colorImageInfo1;
    
    device.updateDescriptorSets(writes);
}

void Voronoi::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Voronoi::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Voronoi::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .geometryShader(resource("circle.geom.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .compareOpAlways()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(constants))
                    .addDescriptorSetLayout(descriptorSetLayout)
                .name("render")
                .build(render.layout);

    auto builder1 = prototypes->cloneGraphicsPipeline();
    coneRender.pipeline =
        builder1
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec2), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
            .inputAssemblyState()
                .triangleStrip()
            .shaderStage()
                .vertexShader(resource("voronoi_render.vert.spv"))
                .fragmentShader(resource("voronoi_render.frag.spv"))
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(constants))
                .addDescriptorSetLayout(voronoiRegionsSetLayout)
            .name("cone_render")
            .build(coneRender.layout);

    auto builder2 = prototypes->cloneGraphicsPipeline();
    voronoiCalc.pipeline =
            builder2
                .shaderStage()
                    .vertexShader(resource("cone.vert.spv"))
                    .fragmentShader(resource("cone.frag.spv"))
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(constants))
                    .addDescriptorSetLayout(descriptorSetLayout)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .name("voronoi_regions")
        .build(voronoiCalc.layout);
    //    @formatter:on
}

void Voronoi::createComputePipeline() {
    auto module = device.createShaderModule( resource("compute_centroid1.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    // Compute Centroid
    compute_centroid.layout = device.createPipelineLayout( { descriptorSetLayout },  { { VK_SHADER_STAGE_ALL, 0, sizeof(constants) }});
    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute_centroid.layout.handle;
    compute_centroid.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_centroid", compute_centroid.pipeline.handle);

    // Accumulate
    module = device.createShaderModule( resource("accumulate1.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeArea.layout = device.createPipelineLayout( { voronoiRegionsSetLayout, descriptorSetLayout },  { { VK_SHADER_STAGE_ALL, 0, sizeof(constants) }});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = computeArea.layout.handle;
    computeArea.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sum_area", computeArea.pipeline.handle);

    // Histogram
    module = device.createShaderModule( resource("histogram.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    histogram.layout = device.createPipelineLayout( { descriptorSetLayout }, { { VK_SHADER_STAGE_ALL, 0, sizeof(constants) }});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = histogram.layout.handle;
    histogram.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_histogram", histogram.pipeline.handle);

    // Reorder
    module = device.createShaderModule( resource("reorder.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    reorder.layout = device.createPipelineLayout( { descriptorSetLayout }, { { VK_SHADER_STAGE_ALL, 0, sizeof(constants) }});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = reorder.layout.handle;
    reorder.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("reorder_regions", reorder.pipeline.handle);

    // Seed image
    module = device.createShaderModule( resource("seed_image.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    seedVoronoiImage.layout = device.createPipelineLayout( { descriptorSetLayout, voronoiRegionsSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = seedVoronoiImage.layout.handle;
    seedVoronoiImage.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("seed_voronoi_image", seedVoronoiImage.pipeline.handle);

    // jump flood
    module = device.createShaderModule( resource("jump_flood.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    jumpFlood.layout = device.createPipelineLayout( { descriptorSetLayout, voronoiRegionsSetLayout }, { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::ivec2) }});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = jumpFlood.layout.handle;
    jumpFlood.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("jump_flood_algorithm", jumpFlood.pipeline.handle);
}

void Voronoi::onSwapChainDispose() {
    dispose(render.pipeline);
}

void Voronoi::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *Voronoi::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    renderCones(commandBuffer);
//    renderCentroids(commandBuffer);
    renderGenerators(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    generateVoronoiRegions(commandBuffer);
    computeRegionAreas(commandBuffer);
    computeHistogram(commandBuffer);
    computePartialSum(commandBuffer);
    reorderRegions(commandBuffer);
    convergeToCentroid(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Voronoi::renderCones(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coneRender.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, coneRender.layout.handle, 0, 1, &voronoiDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, coneRender.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, clipVertices, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void Voronoi::renderGenerators(VkCommandBuffer commandBuffer) {
    constants.renderCentroid = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, numGenerators, 0, 0);
}

void Voronoi::renderCentroids(VkCommandBuffer commandBuffer) {
    constants.renderCentroid = 1;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdDraw(commandBuffer, 1, numGenerators, 0, 0);
}

void Voronoi::convergeToCentroid(VkCommandBuffer commandBuffer) {
    addCentroidWriteBarrier(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_centroid.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_centroid.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute_centroid.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, numGenerators, 1, 1);
    addCentroidReadBarrier(commandBuffer);
}

void Voronoi::computeRegionAreas(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = voronoiDescriptorSet;
    sets[1] = descriptorSet;

    vkCmdFillBuffer(commandBuffer, regions, 0, regions.size, 0);
    vkCmdFillBuffer(commandBuffer, counts, 0, counts.size, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeArea.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computeArea.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, computeArea.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, width, height, 1);
    prefixSum.addBufferMemoryBarriers(commandBuffer, {&regions, &counts});
}

void Voronoi::computeHistogram(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, histogram.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, histogram.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, histogram.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, width, height, 1);
    prefixSum.addBufferMemoryBarriers(commandBuffer, {&counts});
}

void Voronoi::reorderRegions(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reorder.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reorder.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, reorder.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, width, height, 1);

    VkBufferCopy copyRegion{0, 0, regionReordered.size};
    vkCmdCopyBuffer(commandBuffer, regionReordered, regions, 1, &copyRegion);

}

void Voronoi::computePartialSum(VkCommandBuffer commandBuffer) {
    prefixSum.inclusive(commandBuffer, counts, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void Voronoi::addCentroidWriteBarrier(VkCommandBuffer commandBuffer) {
    VkBufferMemoryBarrier barrier = initializers::bufferMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = centroids;
    barrier.offset = 0;
    barrier.size = centroids.size;

    std::vector<VkBufferMemoryBarrier> barriers{};
    barriers.push_back(barrier);

    barrier.buffer = regions;
    barrier.size = regions.size;
    barriers.push_back(barrier);

    barrier.buffer = counts;
    barrier.size = counts.size;
    barriers.push_back(barrier);

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, barriers.size(), barriers.data(), 0, nullptr);
}

void Voronoi::addCentroidReadBarrier(VkCommandBuffer commandBuffer) {
    VkBufferMemoryBarrier barrier = initializers::bufferMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.buffer = centroids;
    barrier.offset = 0;
    barrier.size = centroids.size;

    std::vector<VkBufferMemoryBarrier> barriers{};
    barriers.push_back(barrier);

    barrier.buffer = regions;
    barrier.size = regions.size;
    barriers.push_back(barrier);

    barrier.buffer = counts;
    barrier.size = counts.size;
    barriers.push_back(barrier);

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, barriers.size(), barriers.data(), 0, nullptr);

}

void Voronoi::addVoronoiImageWriteBarrier(VkCommandBuffer commandBuffer) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = gBuffer.color.image;
    barrier.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

}

void Voronoi::addVoronoiImageReadToWriteBarrier(VkCommandBuffer commandBuffer) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = gBuffer.color.image;
    barrier.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Voronoi::addVoronoiImageWriteToReadBarrier(VkCommandBuffer commandBuffer) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = gBuffer.color.image;
    barrier.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

}

void Voronoi::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void Voronoi::checkAppInputs() {
    camera->processInput();
}

void Voronoi::cleanup() {
    VulkanBaseApp::cleanup();
}

void Voronoi::onPause() {
    VulkanBaseApp::onPause();
}

void Voronoi::endFrame() {
    static bool once = false;

    if(once) {
        VulkanBuffer stagingBuffer = device.createStagingBuffer(width * height * sizeof(glm::vec4));
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {

            VkImageSubresourceLayers subResource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            VkBufferImageCopy region{0, 0, 0, subResource, {0, 0, 0}, { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1}};
            vkCmdCopyImageToBuffer(commandBuffer, gBuffer.color.image, VK_IMAGE_LAYOUT_GENERAL, stagingBuffer, 1, &region);

            VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT,
                                    VK_ACCESS_HOST_READ_BIT};
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                                 &barrier, 0, nullptr, 0, nullptr);
        });

        std::span<glm::vec4> colors = { reinterpret_cast<glm::vec4*>(stagingBuffer.map()), static_cast<size_t>(width * height) };

        std::set<int> idSet;

        for(const auto& color : colors) {
            idSet.insert(int(color.a));
        }

        for(auto i = 0; i < numGenerators; i++) {
            assert(idSet.contains(i));
        }

        stagingBuffer.unmap();
        once = false;
    }
}



void Voronoi::generateVoronoiRegions(VkCommandBuffer commandBuffer) {
    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.flags = 0;
    info.renderArea = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;

    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = gBuffer.color.imageView.handle;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {0.f, 0.f, 0.f, 0.f};
    info.pColorAttachments = &colorAttachment;

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = gBuffer.depth.imageView.handle;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1, 0u};
    info.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &info);

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, voronoiCalc.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, voronoiCalc.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, voronoiCalc.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cone.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cone.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, cone.indices.sizeAs<int>(), numGenerators, 0, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

void Voronoi::generateVoronoiRegions2(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = descriptorSet;
    sets[1] = voronoiDescriptorSet;

    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkClearColorValue clearColor{ -1.f, -1.f, -1.f, -1.f};

    addVoronoiImageReadToWriteBarrier(commandBuffer);

    vkCmdClearColorImage(commandBuffer, gBuffer.color.image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &range);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, seedVoronoiImage.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, seedVoronoiImage.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, numGenerators, 1, 1);

    addVoronoiImageWriteBarrier(commandBuffer);
    auto w = gBuffer.color.width;
    auto h = gBuffer.color.height;
    auto size = glm::max(w, h);
    int numPasses = int(glm::log2(float(size)));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, jumpFlood.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, jumpFlood.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    for(int i = 1; i <= numPasses; i++) {
        auto x = float(i);
        glm::ivec2 k{ w * glm::pow(2, -x), h * glm::pow(2, -x) };
        vkCmdPushConstants(commandBuffer, jumpFlood.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(k), &k);
        vkCmdDispatch(commandBuffer, w, h, 1);

        if(i != numPasses) {
            addVoronoiImageWriteBarrier(commandBuffer);
        }
    }
    addVoronoiImageWriteToReadBarrier(commandBuffer);

}

int main(){
    try{

        Settings settings;
        settings.height = 128;
        settings.width = 128;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = Voronoi{settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}