#include "WeightedVoronoiStippling.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "L2DFileDialog.h"
#include "primitives.h"

WeightedVoronoiStippling::WeightedVoronoiStippling(const Settings& settings) : VulkanBaseApp("Weighted Voronoi Stippling", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/weighted_voronoi_stippling");
    fileManager.addSearchPathFront("../../examples/weighted_voronoi_stippling/spv");
    fileManager.addSearchPathFront("../../examples/weighted_voronoi_stippling/data");
    fileManager.addSearchPathFront("../../examples/weighted_voronoi_stippling/models");
    fileManager.addSearchPathFront("../../examples/weighted_voronoi_stippling/textures");

    static VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    deviceCreateNextChain = &dynamicRenderingFeatures;
}

void WeightedVoronoiStippling::initApp() {
    initCamera();
    initBuffers();
    initPrefixSum();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createComputePipeline();
}

void WeightedVoronoiStippling::initBuffers() {
    points = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU, sizeof(glm::vec2) * MaxPoints, "generator_points");

    globals.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(GlobalData), "globals");
    globals.cpu = reinterpret_cast<GlobalData*>(globals.gpu.map());
    globals.cpu->numPoints = 0;
    globals.cpu->maxPoints = 200;
    globals.cpu->convergenceRate = 0.01;

    auto aCone = primitives::cone(100, 100, 0.5, 1.0, glm::vec4(1), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    std::vector<glm::vec3> vertices{};
    for(const auto& vertex : aCone.vertices) {
        vertices.push_back(vertex.position.xyz());
    }
    cone.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    cone.indices = device.createDeviceLocalBuffer(aCone.indices.data(), BYTE_SIZE(aCone.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void WeightedVoronoiStippling::initPrefixSum() {
    prefixSum = PrefixSum{ &device };
    prefixSum.init();
}

void WeightedVoronoiStippling::initTexture(Texture &texture, uint32_t width, uint32_t height, VkFormat format
                                           , VkImageAspectFlags  aspect, VkImageUsageFlags usage) {
    VkImageCreateInfo imageSpec = initializers::imageCreateInfo(
            VK_IMAGE_TYPE_2D,
            format,
            usage | VK_IMAGE_USAGE_SAMPLED_BIT,
            width,
            height);

    VkImageSubresourceRange subresourceRange = initializers::imageSubresourceRange(aspect);
    texture.image = device.createImage(imageSpec);
    texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

    texture.imageView = texture.image.createView(imageSpec.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_WHITE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    texture.sampler = device.createSampler(samplerInfo);
    texture.width = width;
    texture.height = height;
    texture.depth = 1;
}

void WeightedVoronoiStippling::initCamera() {
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


void WeightedVoronoiStippling::createDescriptorPool() {
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

void WeightedVoronoiStippling::createDescriptorSetLayouts() {
    source.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

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
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();
}

void WeightedVoronoiStippling::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ source.descriptorSetLayout, descriptorSetLayout });
    source.descriptorSet = sets[0];
    descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo globalInfo{ globals.gpu, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &globalInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo pointsInfo{ points, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &pointsInfo;

    device.updateDescriptorSets(writes);

}

void WeightedVoronoiStippling::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void WeightedVoronoiStippling::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void WeightedVoronoiStippling::createRenderPipeline() {
    //    @formatter:off
    uint32_t sWidth = stipple.points.width;
    auto builder = prototypes->cloneGraphicsPipeline();
    uint32_t sHeight = stipple.points.height;
    stipple.render.pipeline =
            builder
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .viewportState()
                    .clear()
                    .viewport()
                        .origin(0, 0)
                        .dimension(sWidth, sHeight)
                        .minDepth(0)
                        .maxDepth(1)
                    .scissor()
                        .offset(0, 0)
                        .extent(sWidth, sHeight)
                    .add()
                .shaderStage()
                    .vertexShader(resource("point.vert.spv"))
                    .geometryShader(resource("point.geom.spv"))
                    .fragmentShader(resource("point.frag.spv"))
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .compareOpAlways()
                .layout().clear()
                    .addDescriptorSetLayout(descriptorSetLayout)
                .dynamicRenderPass()
                    .addColorAttachment(VK_FORMAT_R8G8B8A8_UNORM)
                .name("render_stipple")
            .build(stipple.render.layout);

    auto builder2 = prototypes->cloneGraphicsPipeline();
    stipple.voronoi.pipeline =
        builder2
            .shaderStage()
                .vertexShader(resource("voronoi.vert.spv"))
                .fragmentShader(resource("voronoi.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
            .viewportState()
                .clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(sWidth, sHeight)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(sWidth, sHeight)
                .add()
            .layout().clear()
                .addDescriptorSetLayout(descriptorSetLayout)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .name("voronoi_regions")
            .build(stipple.voronoi.layout);

    auto builder3 = prototypes->cloneGraphicsPipeline();
    debug.pipeline =
        builder3
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .shaderStage()
                    .vertexShader(resource("debug.vert.spv"))
                    .fragmentShader(resource("debug.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(source.descriptorSetLayout)
                    .addDescriptorSetLayout(descriptorSetLayout)
                .name("debug")
                .build(debug.layout);
    //    @formatter:on
}

void WeightedVoronoiStippling::createComputePipeline() {
    auto module = device.createShaderModule(resource("generate_points.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    // Generator points
    stipple.generatePoints.layout = device.createPipelineLayout( { source.descriptorSetLayout, descriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = stipple.generatePoints.layout.handle;

    stipple.generatePoints.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_points", stipple.generatePoints.pipeline.handle);

    // Accumulate
    module = device.createShaderModule(resource("accumulate.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    stipple.sum.layout = device.createPipelineLayout( { source.descriptorSetLayout, descriptorSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = stipple.sum.layout.handle;

    stipple.sum.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("sum_regions", stipple.sum.pipeline.handle);

    // Compute centroid
    module = device.createShaderModule(resource("compute_centroid.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    stipple.computeCentroid.layout = device.createPipelineLayout( { descriptorSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = stipple.computeCentroid.layout.handle;

    stipple.computeCentroid.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("converge_to_centroid", stipple.computeCentroid.pipeline.handle);

    // Histogram
    module = device.createShaderModule( resource("histogram.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    histogram.layout = device.createPipelineLayout( { descriptorSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = histogram.layout.handle;
    histogram.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("compute_histogram", histogram.pipeline.handle);

    // Reorder
    module = device.createShaderModule( resource("reorder.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    reorder.layout = device.createPipelineLayout( { descriptorSetLayout } );
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = reorder.layout.handle;
    reorder.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("reorder_regions", reorder.pipeline.handle);

}


void WeightedVoronoiStippling::onSwapChainDispose() {
    dispose(stipple.render.pipeline);
    dispose(stipple.generatePoints.pipeline);
}

void WeightedVoronoiStippling::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *WeightedVoronoiStippling::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {1.0, 1.0, 1.0, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if(stipple.pointsId) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = source.descriptorSet;
        sets[1] = descriptorSet;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, debug.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdDraw(commandBuffer, 1, 1, 0, 0);
    }

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void WeightedVoronoiStippling::generateVoronoiRegions(VkCommandBuffer commandBuffer) {
    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.flags = 0;
    info.renderArea = {{0, 0}, { source.texture.width, source.texture.height }};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;

    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = stipple.voronoiRegions.imageView.handle;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {0.f, 0.f, 0.f, 0.f};
    info.pColorAttachments = &colorAttachment;

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depthAttachment.imageView = stipple.voronoi.depthBuffer.imageView.handle;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1, 0u};
    info.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &info);

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stipple.voronoi.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, stipple.voronoi.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cone.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, cone.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, cone.indices.sizeAs<int>(), globals.cpu->numPoints, 0, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

void WeightedVoronoiStippling::computeRegionAreas(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = source.descriptorSet;
    sets[1] = descriptorSet;

    vkCmdFillBuffer(commandBuffer, regions, 0, regions.size, 0);
    vkCmdFillBuffer(commandBuffer, counts, 0, counts.size, 0);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stipple.sum.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stipple.sum.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, source.texture.width, source.texture.height, 1);
}

void WeightedVoronoiStippling::convergeToCentroid(VkCommandBuffer commandBuffer) {
    addCentroidWriteBarrier(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stipple.computeCentroid.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stipple.computeCentroid.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, globals.cpu->numPoints, 1, 1);
    addCentroidReadBarrier(commandBuffer);
}

void WeightedVoronoiStippling::addCentroidWriteBarrier(VkCommandBuffer commandBuffer) {
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

void WeightedVoronoiStippling::addCentroidReadBarrier(VkCommandBuffer commandBuffer) {}

void WeightedVoronoiStippling::renderStipples(VkCommandBuffer commandBuffer) {
    VkRenderingInfo info{ VK_STRUCTURE_TYPE_RENDERING_INFO };
    info.flags = 0;
    info.renderArea = {{0, 0}, { stipple.points.width,  stipple.points.height }};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;

    VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    colorAttachment.imageView = stipple.points.imageView.handle;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {1.f, 1.f, 1.f, 1.f};
    info.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &info);

    auto& pipeline = stipple.render;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDraw(commandBuffer, 1, globals.cpu->numPoints, 0, 0);

    vkCmdEndRendering(commandBuffer);
}

void WeightedVoronoiStippling::renderUI(VkCommandBuffer commandBuffer) {


    if(ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if(ImGui::MenuItem("Open File...", nullptr, false, true)) {
                FileDialog::file_dialog_open = true;
                FileDialog::file_dialog_open_type = FileDialog::FileDialogType::OpenFile;
            }

            if(ImGui::MenuItem("Exit")){
                this->exit->press();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    openFileDialog();


    auto ar = static_cast<float>(source.texture.width)/static_cast<float>(source.texture.height);
    auto w = 512.f;
    auto h = w/ar;
    if(source.textureId){
        ImGui::Begin(source.name.c_str());
        ImGui::SetWindowSize({0, 0});

        ImGui::Image(source.textureId, {w, h});
        ImGui::SliderInt("points", &globals.cpu->maxPoints, 100, MaxPoints);
        ImGui::SliderFloat("speed", &globals.cpu->convergenceRate, 0.01, 1.0);

        if(!stipple.run && ImGui::Button("Stipple")){
            stipple.run = true;
        }
        ImGui::End();
    }

    if(stipple.pointsId){
        ImGui::Begin("stippled image");
        ImGui::SetWindowSize({0, 0});

        ImGui::Image(stipple.pointsId, {w, h});
        ImGui::End();
    }

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void WeightedVoronoiStippling::openFileDialog() {
    static char* file_dialog_buffer = nullptr;
    static char path[500] = "";

    static bool once = true;
    if(once) {
        auto pwd = std::filesystem::current_path().string();
        std::memcpy(path, pwd.c_str(), pwd.length());
        once = false;
    }
    file_dialog_buffer = path;

    static bool closed = false;
    if (FileDialog::file_dialog_open) {
        FileDialog::ShowFileDialog(&FileDialog::file_dialog_open, file_dialog_buffer, &closed, FileDialog::file_dialog_open_type);
    }

    if(closed) {
        imagePath = std::filesystem::path{path};
        closed = false;
    }
}

void WeightedVoronoiStippling::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    glfwSetWindowTitle(window, fmt::format("{} - fps: {}", title, framePerSecond).c_str());
}

void WeightedVoronoiStippling::checkAppInputs() {
    camera->processInput();
}

void WeightedVoronoiStippling::cleanup() {
    VulkanBaseApp::cleanup();
}

void WeightedVoronoiStippling::onPause() {
    VulkanBaseApp::onPause();
}

void WeightedVoronoiStippling::endFrame() {
    if(imagePath.has_value()) {
        if(source.textureId){
            source.textureId = nullptr;
            stipple.pointsId = nullptr;
            stipple.voronoiRegionsId = nullptr;
            invalidateSwapChain();
            return;
        }
        loadImage(*imagePath);

        const auto w = source.texture.width;
        const auto h = source.texture.height;
        initTexture(stipple.points, w, h, VK_FORMAT_R8G8B8A8_UNORM);
        initTexture(stipple.voronoiRegions, w, h, VK_FORMAT_R32G32B32A32_SFLOAT);
        initTexture(stipple.voronoi.depthBuffer, w, h, VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        
        createRenderPipeline();
        globals.cpu->projection = glm::ortho(0.f, float(stipple.points.width), 0.f, float(stipple.points.height));
        globals.cpu->width = stipple.points.width;
        globals.cpu->height = stipple.points.height;
        imagePath = {};
    }

    if(stipple.run){
        generateStipple();
        initScratchData();
        stipple.pointsId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(stipple.points.imageView);
        stipple.voronoiRegionsId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(stipple.voronoiRegions);

        stipple.run = false;
    }

    static bool once = true;
    if(stipple.pointsId && once){
//        once = false;
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            generateVoronoiRegions(commandBuffer);
            computeRegionAreas(commandBuffer);
            computeHistogram(commandBuffer);
            computePartialSum(commandBuffer);
            reorderRegions(commandBuffer);
            convergeToCentroid(commandBuffer);
            renderStipples(commandBuffer);
        });
    }
}

void WeightedVoronoiStippling::computeHistogram(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, histogram.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, histogram.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, source.texture.width, source.texture.height, 1);
    prefixSum.addBufferMemoryBarriers(commandBuffer, {&counts});
}

void WeightedVoronoiStippling::reorderRegions(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reorder.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, reorder.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(commandBuffer, source.texture.width, source.texture.height, 1);

    VkBufferCopy copyRegion{0, 0, regionReordered.size};
    vkCmdCopyBuffer(commandBuffer, regionReordered, regions, 1, &copyRegion);

}

void WeightedVoronoiStippling::computePartialSum(VkCommandBuffer commandBuffer) {
    prefixSum.inclusive(commandBuffer, counts, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void WeightedVoronoiStippling::generateStipple() {
    globals.cpu->numPoints = 0;
//    while(globals.cpu->numPoints < globals.cpu->maxPoints) {
        generatePoints();
//    }
    spdlog::info("generated {} points", globals.cpu->numPoints);
}

void WeightedVoronoiStippling::initScratchData() {
    int numPoints = globals.cpu->numPoints;
    auto imageSize = source.texture.width * source.texture.height;

    regions = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec4) * imageSize);
    regionReordered = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec4) * imageSize);
    counts = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(int) * numPoints);
    centroids = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec2) * numPoints);

    auto writes = initializers::writeDescriptorSets<5>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 2;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo regionInfo{ regions, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &regionInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 3;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo countInfo{ counts, 0, VK_WHOLE_SIZE };
    writes[1].pBufferInfo = &countInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 4;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo centroidInfo{ centroids, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &centroidInfo;

    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 5;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo regionROInfo{ regionReordered, 0, VK_WHOLE_SIZE };
    writes[3].pBufferInfo = &regionROInfo;


    writes[4].dstSet = descriptorSet;
    writes[4].dstBinding = 6;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo voronoiInfo{ stipple.voronoiRegions.sampler.handle, stipple.voronoiRegions.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[4].pImageInfo = &voronoiInfo;

    device.updateDescriptorSets(writes);

    prefixSum.updateDataDescriptorSets(counts);

    // TODO update descriptorSet
}

void WeightedVoronoiStippling::generatePoints() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = source.descriptorSet;
        sets[1] = descriptorSet;

        vkCmdBindPipeline(commandBuffer , VK_PIPELINE_BIND_POINT_COMPUTE, stipple.generatePoints.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, stipple.generatePoints.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdDispatch(commandBuffer, globals.cpu->maxPoints, 1, 1);

        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
    });

}

void WeightedVoronoiStippling::loadImage(const std::filesystem::path& path) {
    textures::fromFile(device, source.texture, path.string());
    source.name = path.filename().string();
    source.textureId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(source.texture.imageView);

    auto writes = initializers::writeDescriptorSets();
    writes[0].dstSet = source.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo info{
        source.texture.sampler.handle,
        source.texture.imageView.handle,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    writes[0].pImageInfo = &info;

    device.updateDescriptorSets(writes);
    spdlog::info("{} successfully loaded", path.string());
}


int main(){
    try{

        Settings settings;
        settings.width = 2048;
        settings.height = 1536;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();

        auto app = WeightedVoronoiStippling{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}