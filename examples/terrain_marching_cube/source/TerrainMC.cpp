#include "TerrainMC.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

TerrainMC::TerrainMC(const Settings& settings) : VulkanBaseApp("Marching Cube Terrain", settings) {
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("data/shaders");
    fileManager().addSearchPathFront("/terrain_marching_cube");
    fileManager().addSearchPathFront("/terrain_marching_cube/data");
    fileManager().addSearchPathFront("/terrain_marching_cube/spv");
    fileManager().addSearchPathFront("/terrain_marching_cube/models");
    fileManager().addSearchPathFront("/terrain_marching_cube/textures");
}

void TerrainMC::initApp() {
    checkInvariants();
    createCube();
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void TerrainMC::checkInvariants() {
    assert(sizeof(CameraInfo) == 260);
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

//    debugCamera.proj = vkn::ortho(-20, 20, -20, 20, 0.5, 1000);
//    debugCamera.view = glm::lookAt({0, 20, 0}, glm::vec3(0), {0, 0, -1});
    debugCamera.proj = vkn::perspective(glm::radians(90.f), swapChain.aspectRatio(), 1, 1000);
    debugCamera.view = glm::lookAt({0, 0, 20}, glm::vec3(0), {0, 1, 0});

    auto xform = glm::translate(glm::mat4{1}, {0, 0, 0.5});
    xform = glm::scale(xform, {1, 1, 0.5});
    cBounds = primitives::cubeOutline({0, 1, 1, 1}, xform);

    cameraBounds.vertices = device.createDeviceLocalBuffer(cBounds.vertices.data(), BYTE_SIZE(cBounds.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    cameraView.gpu.resize(MAX_IN_FLIGHT_FRAMES);
    cameraView.info.resize(MAX_IN_FLIGHT_FRAMES);

    for(auto i = 0; i <MAX_IN_FLIGHT_FRAMES; ++i) {
        cameraView.gpu[i] = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(CameraInfo), "camera_info");
        cameraView.info[i] = reinterpret_cast<CameraInfo *>(cameraView.gpu[i].map());
    }
}

void TerrainMC::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void TerrainMC::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
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
}

void TerrainMC::updateDescriptorSets(){
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
//                .colorBlendState()
//                    .attachment().clear()
//                    .enableBlend()
//                    .colorBlendOp().add()
//                    .alphaBlendOp().add()
//                    .srcColorBlendFactor().srcAlpha()
//                    .dstColorBlendFactor().oneMinusSrcAlpha()
//                    .srcAlphaBlendFactor().zero()
//                    .dstAlphaBlendFactor().one()
//                .add()
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
//                .colorBlendState()
//                    .attachment().clear()
//                    .enableBlend()
//                    .colorBlendOp().add()
//                    .alphaBlendOp().add()
//                    .srcColorBlendFactor().srcAlpha()
//                    .dstColorBlendFactor().oneMinusSrcAlpha()
//                    .srcAlphaBlendFactor().zero()
//                    .dstAlphaBlendFactor().one()
//                .add()
                .dynamicState()
                    .primitiveTopology()
                .name("camera_render")
                .build(camRender.layout);

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

    for(const auto& transform : visibleList) {
        renderCube(commandBuffer, debugCamera, transform, true);
    }

    renderCube(commandBuffer, debugCamera, glm::translate(glm::mat4(1), {0, 0, -1}) * tinyCube);
    renderCube(commandBuffer, debugCamera, glm::translate(glm::mat4(1), {0, 0, -10}) * tinyCube);

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

    auto dim = glm::ivec3(cameraView.info[currentFrame]->aabbMax - cameraView.info[currentFrame]->aabbMin) + 2;
    auto inverse_view = glm::inverse(camera->cam().view);
    const auto near = camera->near();
    const auto far = camera->far();

    auto n = glm::vec3(dim - 1);
    for(auto z = 0; z < dim.z; ++z) {
        for (auto y = 0; y < dim.y; ++y) {
            for (auto x = 0; x < dim.x; ++x) {
                glm::vec3 position =  glm::vec3(x, y, z);
                position = remap(position, glm::vec3(0), n, -n * 0.5f, n * 0.5f);
                auto transform =
                        inverse_view
                        * glm::translate(glm::mat4(1), {0, 0, -(far - near) * 0.5 - near})
                        * glm::translate(glm::mat4(1), position);
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

    const auto cam = camera->cam();
    cameraView.info[currentFrame]->position = camera->position();
    cameraView.info[currentFrame]->view_projection = cam.proj * cam.view;
    cameraView.info[currentFrame]->inverse_view_projection = glm::inverse(cam.proj * cam.view);
    camera->extract(cameraView.info[currentFrame]->frustum);
    computeCameraBounds();
    updateVisibilityList();
}

void TerrainMC::computeCameraBounds() {
    auto camInfo = cameraView.info[currentFrame];
    camInfo->aabbMin = glm::vec3(MAX_FLOAT);
    camInfo->aabbMax = glm::vec3(MIN_FLOAT);

    const auto near = camera->near();
    const auto far = camera->far();
    const auto aspect = camera->aspectRatio;
    const auto fov = glm::radians(camera->fov);

    glm::vec2 nearCorner{0, 0};
    nearCorner.y = glm::tan(fov / 2) * -near; // TODO check if horizontal or vertical fov
    nearCorner.x = nearCorner.y * aspect;

    static std::array<glm::vec4, 8> corners{};
    corners[0] = glm::vec4(nearCorner, -near, 1);
    corners[1] = glm::vec4(-nearCorner, -near, 1);

    nearCorner.y *= -1;
    corners[2] = glm::vec4(nearCorner, -near, 1);
    corners[3] = glm::vec4(-nearCorner, -near, 1);

    glm::vec2 farCorner{0, 0,};
    farCorner.y = glm::tan(fov / 2) * -far; // TODO check if horizontal or vertical fov
    farCorner.x = farCorner.y * aspect;

    corners[4] = glm::vec4(farCorner, -far, 1);
    corners[5] = glm::vec4(-farCorner, -far, 1);

    farCorner.y *= -1;
    corners[6] = glm::vec4(farCorner, -far, 1);
    corners[7] = glm::vec4(-farCorner, -far, 1);

    auto& bMin = camInfo->aabbMin;
    auto& bMax = camInfo->aabbMax;
    auto inverse_view = glm::inverse(camera->cam().view);

    for(auto& corner : corners) {
        bMin = glm::min(corner.xyz(), bMin);
        bMax = glm::max(corner.xyz(), bMax);
    }

//    bMin = (inverse_view * glm::vec4(bMin, 1)).xyz();
//    bMax = (inverse_view * glm::vec4(bMax, 1)).xyz();

    auto dim = glm::ceil(bMax - bMin);

    cameraBounds.aabb = glm::mat4{1};
    cameraBounds.aabb = glm::translate(cameraBounds.aabb, {0, 0, -dim.z * 0.5 - near});
    cameraBounds.aabb = glm::scale(cameraBounds.aabb, dim);
    cameraBounds.aabb = inverse_view * cameraBounds.aabb;
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


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 720;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = TerrainMC{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}