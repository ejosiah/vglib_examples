#include "VolumeRendering.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "volume_loader.hpp"
#include "ImGuiPlugin.hpp"

VolumeRendering::VolumeRendering(const Settings& settings) : VulkanBaseApp("Volume Rendering", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/volume_rendering");
    fileManager.addSearchPathFront("../../examples/volume_rendering/data");
    fileManager.addSearchPathFront("../../examples/volume_rendering/spv");
    fileManager.addSearchPathFront("../../examples/volume_rendering/models");
    fileManager.addSearchPathFront("../../examples/volume_rendering/textures");
}

void VolumeRendering::initApp() {
    createDescriptorPool();
    initRenderingData();
    loadVolumeData();
    initCamera();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void VolumeRendering::loadVolumeData() {

//    auto [header, dataSet] = noise(device, descriptorPool, fileManager);
    auto [header, dataSet] = load_volume(resource("C60.vol"));
//    auto [header, dataSet] = load_beatle_volume(resource("stagbeetle832x832x494.dat"));
//    auto [header, dataSet] = load_beatle_volume(resource("present492x492x442.dat"));
    rayMarchRenderer.constants.stepSize = 1.f/glm::vec3(header.sizeX, header.sizeY, header.sizeZ);
//    rayMarchRenderer.constants.stepSize = glm::vec3(0.1);

    textures::create(device, volumeTexture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, dataSet.data(),
                     {header.sizeX, header.sizeY, header.sizeZ}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));

}

void VolumeRendering::initRenderingData() {
    auto cube = primitives::cube();
    std::vector<glm::vec4> cubeVertices;
    for(const auto& vertex : cube.vertices){
        cubeVertices.push_back(vertex.position);
    }
    cubeBuffer = device.createCpuVisibleBuffer(cubeVertices.data(), BYTE_SIZE(cubeVertices),
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    cubeIndexBuffer = device.createDeviceLocalBuffer(cube.indices.data(), BYTE_SIZE(cube.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);


    glm::vec4 v{0};
    vertexBuffer = device.createDeviceLocalBuffer(&v, sizeof(v), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

}

void VolumeRendering::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}


void VolumeRendering::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 16> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

}

void VolumeRendering::createDescriptorSetLayouts() {
    volumeDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("volume")
            .binding(0)
            .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void VolumeRendering::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( {volumeDescriptorSetLayout} );
    volumeSet = sets[0];
    
    auto writes = initializers::writeDescriptorSets<1>();
    
    writes[0].dstSet = volumeSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo volumeInfo{volumeTexture.sampler, volumeTexture.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &volumeInfo;

    device.updateDescriptorSets(writes);
}

void VolumeRendering::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumeRendering::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VolumeRendering::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    sliceRenderer.pipeline =
        builder
            .allowDerivatives()
            .shaderStage()
                .vertexShader(resource("slice.vert.spv"))
                .geometryShader(resource("slice.geom.spv"))
                .fragmentShader(resource("slice.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0)
            .inputAssemblyState()
                .points()
            .viewportState()
                .viewport()
                    .origin(0, 0)
                    .dimension(swapChain.extent)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(swapChain.extent)
                .add()
                .rasterizationState()
                    .cullBackFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(settings.msaaSamples)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                        .enableBlend()
                        .colorBlendOp().add()
                        .alphaBlendOp().add()
                        .srcColorBlendFactor().srcAlpha()
                        .dstColorBlendFactor().oneMinusSrcAlpha()
                        .srcAlphaBlendFactor().one()
                        .dstAlphaBlendFactor().zero()
                    .add()
                .layout()
                    .addDescriptorSetLayout(volumeDescriptorSetLayout)
                    .addPushConstantRange(VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(sliceRenderer.constants))
                .renderPass(renderPass)
                .subpass(0)
                .name("volume_slice_renderer")
                .pipelineCache(pipelineCache)
            .build(sliceRenderer.layout);

    rayMarchRenderer.pipeline =
        builder
            .basePipeline(sliceRenderer.pipeline)
            .shaderStage().clear()
                .vertexShader(resource("ray_march.vert.spv"))
                .fragmentShader(resource("ray_march.frag.spv"))
            .vertexInputState()
            .inputAssemblyState()
                .triangles()
//            .rasterizationState()
//                .cullNone()
            .layout().clear()
//                .addPushConstantRange(Camera::pushConstant())
                .addDescriptorSetLayout(volumeDescriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4))
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4), sizeof(rayMarchRenderer.constants) - sizeof(glm::mat4))
            .name("volume_ray_marcher")
        .build(rayMarchRenderer.layout);
    //    @formatter:on
}

void VolumeRendering::createComputePipeline() {
    auto module = VulkanShaderModule{ "../../data/shaders/pass_through.comp.spv", device};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache);
}


void VolumeRendering::onSwapChainDispose() {
    dispose(sliceRenderer.pipeline);
    dispose(compute.pipeline);
}

void VolumeRendering::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *VolumeRendering::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

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

    renderVolume(commandBuffer);
    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumeRendering::renderVolume(VkCommandBuffer commandBuffer) {
    if(volumeRenderMethod == VolumeRenderMethod::Slice) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sliceRenderer.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sliceRenderer.layout, 0, 1, &volumeSet,
                                0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, sliceRenderer.layout, VK_SHADER_STAGE_GEOMETRY_BIT, 0,
                           sizeof(sliceRenderer.constants), &sliceRenderer.constants);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffer, &offset);
        vkCmdDraw(commandBuffer, 1, sliceRenderer.constants.numSlices, 0, 0);
    }else{
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarchRenderer.pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarchRenderer.layout, 0, 1, &volumeSet,
                                0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, rayMarchRenderer.layout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::mat4), &rayMarchRenderer.constants);
        vkCmdPushConstants(commandBuffer, rayMarchRenderer.layout, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(glm::mat4),
                           sizeof(rayMarchRenderer.constants) - sizeof(glm::mat4), &rayMarchRenderer.constants.camPos);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cubeBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, cubeIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, cubeIndexBuffer.sizeAs<uint32_t>(), 1, 0, 0, 0);
    }
}

void VolumeRendering::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("volume Rendering");
    ImGui::SetWindowSize({0, 0});

    static int method = static_cast<int>(volumeRenderMethod);
    ImGui::Text("Renderer:"); ImGui::SameLine();
    ImGui::RadioButton("slice", &method, static_cast<int>(VolumeRenderMethod::Slice)); ImGui::SameLine();
    ImGui::RadioButton("RayMatch", &method, static_cast<int>(VolumeRenderMethod::RayMarch));

    volumeRenderMethod = static_cast<VolumeRenderMethod>(method);
    if(volumeRenderMethod == VolumeRenderMethod::Slice) {
        ImGui::SliderInt("num slices", &sliceRenderer.constants.numSlices, 64, 4096);
    }
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VolumeRendering::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    sliceRenderer.constants.mvp = cam.proj * cam.view * cam.model;
    sliceRenderer.constants.viewDir = camera->viewDir;
    rayMarchRenderer.constants.mvp = sliceRenderer.constants.mvp;
    rayMarchRenderer.constants.camPos = camera->position();

    auto msg = fmt::format("{} - FPS {}", title, framePerSecond);
    glfwSetWindowTitle(window, msg.c_str());
}

void VolumeRendering::checkAppInputs() {
    camera->processInput();
}

void VolumeRendering::cleanup() {
    VulkanBaseApp::cleanup();
}

void VolumeRendering::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.wideLines = VK_TRUE;
        auto app = VolumeRendering{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}