#include "PhysicalSimulation.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"

PhysicalSimulation::PhysicalSimulation(const Settings& settings) : VulkanBaseApp("Fluid simulation", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("physical_simulation");
    fileManager().addSearchPathFront("physical_simulation/data");
    fileManager().addSearchPathFront("physical_simulation/spv");
    fileManager().addSearchPathFront("physical_simulation/models");
    fileManager().addSearchPathFront("physical_simulation/textures");
}

void PhysicalSimulation::initApp() {
    initCamera();
    initSimData();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void PhysicalSimulation::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 45.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

//    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({0, 0, -160}, glm::vec3(0), {0, 1, 0});
}

void PhysicalSimulation::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void PhysicalSimulation::beforeDeviceCreation() {
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

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if(devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        devFeatures12.scalarBlockLayout = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void PhysicalSimulation::createDescriptorPool() {
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


void PhysicalSimulation::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void PhysicalSimulation::createDescriptorSetLayouts() {
    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("physical_simulation")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void PhysicalSimulation::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ descriptorSetLayout });
    descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<4>();
    
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo positionInfo{ data.position, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &positionInfo;

    writes[1].dstSet = descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo velocityInfo{ data.velocity, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &velocityInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo attractorsInfo{ data.attractors, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &attractorsInfo;

    writes[3].dstSet = descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo globalsInfo{ data.globals.gpu, 0, VK_WHOLE_SIZE};
    writes[3].pBufferInfo = &globalsInfo;

    device.updateDescriptorSets(writes);
}

void PhysicalSimulation::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void PhysicalSimulation::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void PhysicalSimulation::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .vertexInputState().clear()
                .inputAssemblyState()
                    .points()
                .colorBlendState()
                    .attachment().clear()
                    .enableBlend()
                    .colorBlendOp().add()
                    .alphaBlendOp().add()
                    .srcColorBlendFactor().one()
                    .dstColorBlendFactor().one()
                    .srcAlphaBlendFactor().one()
                    .dstAlphaBlendFactor().one()
                .add()
                .layout()
                    .addDescriptorSetLayout(descriptorSetLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}

void PhysicalSimulation::createComputePipeline() {
    auto module = device.createShaderModule(resource("integrate.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.integrate.layout = device.createPipelineLayout({ descriptorSetLayout } );

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.integrate.layout.handle;

    compute.integrate.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    // update attractors
    module = device.createShaderModule(resource("update_attractors.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.update_attractors.layout = device.createPipelineLayout({ descriptorSetLayout } );

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.update_attractors.layout.handle;

    compute.update_attractors.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

}


void PhysicalSimulation::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.integrate.pipeline);
    dispose(compute.update_attractors.pipeline);
}

void PhysicalSimulation::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *PhysicalSimulation::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    runSim(commandBuffer);

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

    renderParticles(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void PhysicalSimulation::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    data.globals.cpu->delta_time = time * 0.075f;
    data.globals.cpu->time += time * 0.005f;
}

void PhysicalSimulation::checkAppInputs() {
    camera->processInput();
}

void PhysicalSimulation::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void PhysicalSimulation::onPause() {
    VulkanBaseApp::onPause();
}

void PhysicalSimulation::initSimData() {
    std::vector<glm::vec4> positions(NUM_PARTICLES);
    std::vector<glm::vec3> velocity(NUM_PARTICLES);
    std::vector<glm::vec4> attractors(MAX_ATTRACTORS);

    auto xi = rng(0, 1);
    auto p = rng(-10, 10);
    std::generate(positions.begin(), positions.end(), [&]{
        return glm::vec4{p(), p(), p(), xi()};
    });

    auto v = rng(-0.1, 0.1);
    std::generate(velocity.begin(), velocity.end(), [&]{
        return glm::vec3{v(), v(), 0};
    });

    auto mass = rng(0.5, 1.0);
    std::generate(attractors.begin(), attractors.end(), [&]{
        return glm::vec4{0, 0, 0, mass()};
    });

    data.position = device.createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    data.velocity = device.createDeviceLocalBuffer(velocity.data(), BYTE_SIZE(velocity), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    data.attractors = device.createDeviceLocalBuffer(attractors.data(), BYTE_SIZE(attractors), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    auto globals = Globals{};
    data.globals.gpu = device.createCpuVisibleBuffer(&globals, sizeof(Globals), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    data.globals.cpu = reinterpret_cast<Globals*>(data.globals.gpu.map());
}

void PhysicalSimulation::renderParticles(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, 0);
    camera->push(commandBuffer, render.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &emptyVertexBuffer.buffer, &offset);
    vkCmdDraw(commandBuffer, 1, NUM_PARTICLES, 0, 0);
}

void PhysicalSimulation::runSim(VkCommandBuffer commandBuffer) {

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.update_attractors.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.update_attractors.layout.handle, 0, 1, &descriptorSet, 0, 0);
    vkCmdDispatch(commandBuffer, 1, 1, 1);

    Barrier::computeWriteToRead(commandBuffer);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.integrate.layout.handle, 0, 1, &descriptorSet, 0, 0);
    vkCmdDispatch(commandBuffer, NUM_PARTICLES/128, 1, 1);

    Barrier::computeWriteToFragmentRead(commandBuffer);

}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1440;
        settings.height = 1280;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
//        settings.vSync = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = PhysicalSimulation{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}