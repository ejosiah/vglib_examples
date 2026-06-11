#include "YetAnotherFluidSim.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

YetAnotherFluidSim::YetAnotherFluidSim(const Settings& settings) : VulkanBaseApp("Yet another fluid simulation", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("yet_another_fluid_sim");
    fileManager().addSearchPathFront("yet_another_fluid_sim/data");
    fileManager().addSearchPathFront("yet_another_fluid_sim/spv");
    fileManager().addSearchPathFront("yet_another_fluid_sim/models");
    fileManager().addSearchPathFront("yet_another_fluid_sim/textures");
}

void YetAnotherFluidSim::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    initSceneProperties();
    initSolver();
    createComputePipeline();
    createRenderPipeline();
}

void YetAnotherFluidSim::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = swapChain.aspectRatio();

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void YetAnotherFluidSim::initSceneProperties() {
    scenes[Scene::Tank] = {};

    scenes[Scene::WindTunnel] = {
        .resolution = 100,
        .gravity =  0.0f
        // Smoke/dye
    };

    scenes[Scene::Paint] = {
        .resolution = 100,
        .gravity =  0.0f
        // paint/ Smoke/dye
    };

}

void YetAnotherFluidSim::initSolver() {
    const auto& props = scenes.at(scene);
    const auto aspect = swapChain.aspectRatio();
    solverGridSize = {
        std::max(1u, static_cast<uint32_t>(aspect * float(props.resolution))),
        std::max(1u, props.resolution)
    };

    gravityConstants.gravityY = props.gravity;

    solver =
        eular::FluidSolver::Builder{&device, &descriptorPool}
            .gridSize(solverGridSize)
            .density(1000.0f)
            .dt(props.timeStep)
            .poissonIterations(static_cast<int>(props.iterations))
            .generate([](float, float) { return glm::vec2{0.0f}; })
            .add(gravityForce())
            .useGaussSeidelSolver()
            .ensureBoundaryCondition(true)
        .build();

    initVisualizer();
}

void YetAnotherFluidSim::initVisualizer() {
    visualizer = FieldVisualizer{
        &device, &descriptorPool, &renderPass, solver->fieldDescriptorSetLayout(),
        { width, height }, solverGridSize
    };

    visualizer.init();
    visualizer.set(solver.get());
}

eular::ExternalForce YetAnotherFluidSim::gravityForce() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        gravityConstants.gravityY = scenes.at(scene).gravity;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("gravity"));
        vkCmdPushConstants(commandBuffer, compute.layout("gravity"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gravityConstants), &gravityConstants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("gravity"), 0, COUNT(forceFieldSets), forceFieldSets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer,  gc.x, gc.y, gc.z);
    };
}

void YetAnotherFluidSim::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void YetAnotherFluidSim::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void YetAnotherFluidSim::createDescriptorPool() {
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


void YetAnotherFluidSim::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void YetAnotherFluidSim::createDescriptorSetLayouts() {
}

void YetAnotherFluidSim::updateDescriptorSets(){
}

void YetAnotherFluidSim::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void YetAnotherFluidSim::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void YetAnotherFluidSim::createComputePipeline() {
    forceFieldSetLayouts = solver->forceFieldSetLayouts();
    auto range = VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(gravityConstants)};

    compute = ComputePipelines{&device, {{
        "gravity",
        resource("gravity.comp.spv"),
        {&forceFieldSetLayouts[0], &forceFieldSetLayouts[1]},
        {range}
    }}};
    compute.createPipelines();
}


void YetAnotherFluidSim::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("pass_through.vert.spv"))
                    .fragmentShader(resource("pass_through.frag.spv"))
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void YetAnotherFluidSim::onSwapChainDispose() {
    dispose(render.pipeline);
}

void YetAnotherFluidSim::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *YetAnotherFluidSim::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(1, 1, 1);

    if (advance) {
        solver->runSimulation(commandBuffer);
        advance = false;
    }
    visualizer.update(commandBuffer);

    renderToSwapChain([&]{
        visualizer.renderDebugFields(commandBuffer);
        // visualizer.renderVectorField(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void YetAnotherFluidSim::renderUI(VkCommandBuffer cmdBuf) {
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({});

    advance = ImGui::Button("advance");
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(cmdBuf);
}

void YetAnotherFluidSim::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void YetAnotherFluidSim::checkAppInputs() {
    camera->processInput();
}

void YetAnotherFluidSim::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void YetAnotherFluidSim::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1900;
        settings.height = 881;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = YetAnotherFluidSim{ settings };

        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
