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

void TerrainMC::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.zNear = 0.5;
    cameraSettings.zFar = 10;
    cameraSettings.acceleration = glm::vec3(10);
    cameraSettings.velocity = glm::vec3(20);

    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->position(glm::vec3(0, 0, 5));

    camera->lookAt(glm::vec3(0, 0, 5), glm::vec3(0), {0, 1, 0});
    cameraSettings.zFar = 1000;
    cameraSettings.fieldOfView = 90.f;

    debugCamera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    debugCamera->position({0, 1, 20});
    debugCamera->lookAt({0, 20, 0}, glm::vec3(0), {0, 0, -1});

    auto xform = glm::translate(glm::mat4{1}, {0, 0, 0.5});
    xform = glm::scale(xform, {1, 1, 0.5});
    cBounds = primitives::cubeOutline({0, 1, 1, 1}, xform);

//    auto M = glm::inverse(camera->camera.proj * camera->camera.view);
//    for(auto& v : cBounds.vertices) {
//        v.position = M * v.position;
//        v.position /= v.position.w;
//    }

    cameraBounds.vertices = device.createDeviceLocalBuffer(cBounds.vertices.data(), BYTE_SIZE(cBounds.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        debugCamera->push(commandBuffer, render.layout, transform);
//        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.solid.vertices, &offset);
//        vkCmdBindIndexBuffer(commandBuffer, cube.solid.indexes, 0, VK_INDEX_TYPE_UINT32);
//        vkCmdDrawIndexed(commandBuffer, cube.solid.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, cube.outline.vertices, &offset);
        vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
        vkCmdDraw(commandBuffer, cube.outline.vertices.sizeAs<Vertex>(), 1, 0, 0);
    }

    renderCamera(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void TerrainMC::renderCamera(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
//    auto model = glm::scale(glm::mat4{1}, glm::vec3(0.5)) * glm::inverse(camera->cam().view);
    auto model = glm::mat4{1};
//    auto model = glm::scale(glm::mat4{1}, glm::vec3(0.25));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, camRender.pipeline.handle);
    debugCamera->push(commandBuffer, camRender.layout, cameraBounds.transform);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, cameraBounds.vertices, &offset);

    vkCmdSetPrimitiveTopology(commandBuffer, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    vkCmdDraw(commandBuffer, cameraBounds.vertices.sizeAs<Vertex>(), 1, 0, 0);
}

void TerrainMC::update(float time) {
    static float angle = 0;
    static float speed = 100;
    angle = time * speed;

//    camera->rotate(0.1, 0, 0);
    updateVisibilityList();

    camera->update(time);
//    debugCamera->update(time);


   cameraBounds.transform = glm::inverse(camera->camera.proj * camera->camera.view);

}

void TerrainMC::checkAppInputs() {
    camera->processInput();
//    debugCamera->processInput();
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

    static constexpr auto N = 32;
    static constexpr auto n = float(N - 1);
    auto offset = glm::vec3(0.5);
    for(auto z = 0; z < N; ++z) {
        for (auto y = 0; y < N; ++y) {
            for (auto x = 0; x < N; ++x) {
                glm::vec3 position =  glm::vec3(x, y, z);
                position = remap(position, glm::vec3(0), glm::vec3(n), glm::vec3(-n * 0.5), glm::vec3(n * 0.5));
                auto transform = glm::translate(glm::mat4(1), position);
                cube.instances.push_back(transform);
            }
        }
    }
}

void TerrainMC::updateVisibilityList() {
    visibleList.clear();
    skipList.clear();
    static auto corners = std::array<glm::vec3, 8> {{
        { -0.5, -0.5, -0.5 },
        { 0.5, -0.5, -0.5 },
        { 0.5, -0.5, 0.5 },
        { -0.5, -0.5, 0.5 },
        { -0.5, 0.5, -0.5 },
        { 0.5, 0.5, -0.5 },
        { 0.5, 0.5, 0.5 },
        { -0.5, 0.5, 0.5 },
    }};

    static Frustum frustum{};
    camera->extract(frustum);


    for(auto i = 0; i < cube.instances.size(); ++i) {

        const auto& transform = cube.instances[i];
        const auto center = transform * glm::vec4(0, 0, 0, 1);
        glm::vec3 bMin{MAX_FLOAT};
        glm::vec3 bMax{MIN_FLOAT};

        for(const auto corner : corners) {
            auto p = center.xyz() + corner;
            bMin = glm::min(bMin, p);
            bMax = glm::max(bMax, p);
        }
        if(frustum.test(bMin, bMax)) {
            visibleList.push_back(transform);
        }else {
            skipList.push_back(transform);
        }
    }
    auto vl = visibleList;
    auto sl = skipList;
    auto n = 0;
}



int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1024;
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