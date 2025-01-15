#include "PlanetDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

PlanetDemo::PlanetDemo(const Settings& settings) : VulkanBaseApp("planet", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("planet");
    fileManager().addSearchPathFront("planet/data");
    fileManager().addSearchPathFront("planet/spv");
    fileManager().addSearchPathFront("planet/models");
    fileManager().addSearchPathFront("planet/textures");
    fileManager().addSearchPathFront("data/shaders");
}

void PlanetDemo::initApp() {
    initCamera();
    createGroundPlane();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
//    ground_patch_ = GroundPatch{ device, 1, 12766};
//    ground_patch_.init();
//    ground_patch_.update(*camera);
}

void PlanetDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.acceleration = glm::vec3(0.5);
    cameraSettings.velocity = glm::vec3(1.47);
//    cameraSettings.acceleration = glm::vec3(20 * km);
//    cameraSettings.velocity = glm::vec3(100 * km);
    cameraSettings.zFar = 100'000 * km;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void PlanetDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void PlanetDemo::beforeDeviceCreation() {
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

void PlanetDemo::createDescriptorPool() {
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


void PlanetDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void PlanetDemo::createDescriptorSetLayouts() {
}

void PlanetDemo::updateDescriptorSets(){
}

void PlanetDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void PlanetDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void PlanetDemo::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("terrain.vert.spv"))
                    .tessellationControlShader(resource("terrain.tesc.spv"))
                    .tessellationEvaluationShader(resource("terrain.tese.spv"))
                    .fragmentShader(resource("terrain.frag.spv"))
                .inputAssemblyState()
                    .patches()
                .tessellationState()
                    .patchControlPoints(4)
                    .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
                .rasterizationState()
                    .polygonModeLine()
                    .cullNone()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT))
                .name("terrain")
            .build(render.layout);
    //    @formatter:on
}


void PlanetDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void PlanetDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *PlanetDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    AppContext::renderAtmosphere(commandBuffer, *camera);

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, ground.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, ground.indexes, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, ground.indexes.sizeAs<uint32_t>(), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PlanetDemo::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    setTitle(fmt::format("{} - cam: {}", title, camera->position()));
}

void PlanetDemo::checkAppInputs() {
    camera->processInput();
}

void PlanetDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void PlanetDemo::onPause() {
    VulkanBaseApp::onPause();
}

void PlanetDemo::createGroundPlane() {
    static constexpr int SQRT_NUM_PATCHES = 1;
    static constexpr float PATCH_SIZE = 1 * km;
    int w = SQRT_NUM_PATCHES;
    int h = SQRT_NUM_PATCHES;
    glm::mat4 xform = glm::rotate(glm::mat4{1}, -glm::half_pi<float>(), {1, 0, 0});
//    glm::mat4 xform{1};
    auto plane = primitives::plane(w, h, PATCH_SIZE, PATCH_SIZE, xform, {1, 0, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);


    std::vector<uint32_t> indices;
    for(int j = 0; j < h; j++){
        for(int i = 0; i < w; i++){
            indices.push_back(j * (w + 1) + i);                 // 3
            indices.push_back(j * (w + 1) + i + 1);             // 2
            indices.push_back((j + 1) * (w + 1) + i + 1);       // 1
            indices.push_back((j + 1) * (w + 1) + i);           // 0
        }
    }

//    indices.push_back(0);
//    indices.push_back(1);
//    indices.push_back(3);
//    indices.push_back(2);
    ground.vertices = device.createDeviceLocalBuffer(plane.vertices.data(), BYTE_SIZE(plane.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    ground.indexes = device.createDeviceLocalBuffer(indices.data(), BYTE_SIZE(indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void PlanetDemo::newFrame() {
    camera->_move.forward->press();
}


int main(){
    try{

        Settings settings;
        settings.width = 1024;
        settings.height = 1024;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        fs::current_path("../../../../examples/");
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PlanetDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}