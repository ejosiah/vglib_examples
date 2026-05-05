#include "NeonReactorDemo.hpp"

#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"

NeonReactorDemo::NeonReactorDemo(const Settings& settings) : VulkanBaseApp("Neon Reactor", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("neon_reactor");
    fileManager().addSearchPathFront("neon_reactor/data");
    fileManager().addSearchPathFront("neon_reactor/spv");
    fileManager().addSearchPathFront("neon_reactor/models");
    fileManager().addSearchPathFront("neon_reactor/textures");
}

void NeonReactorDemo::initApp() {
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createUniforms();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    syncUniforms();
}

void NeonReactorDemo::beforeDeviceCreation() {
    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT,
        deviceCreateNextChain
    );
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        deviceCreateNextChain
    );
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void NeonReactorDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 32;
    std::array<VkDescriptorPoolSize, 1> poolSizes{{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets}
    }};
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void NeonReactorDemo::createUniforms() {
    UniformData defaults{};
    uniformBuffer = device.createCpuVisibleBuffer(&defaults, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms = reinterpret_cast<UniformData*>(uniformBuffer.map());
}

void NeonReactorDemo::createDescriptorSetLayouts() {
    uniformDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("neon_reactor_uniforms")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void NeonReactorDemo::updateDescriptorSets() {
    descriptorSet = descriptorPool.allocate({uniformDescriptorSetLayout}).front();

    auto write = initializers::writeDescriptorSets<1>();
    write[0].dstSet = descriptorSet;
    write[0].dstBinding = 0;
    write[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write[0].descriptorCount = 1;
    VkDescriptorBufferInfo bufferInfo{uniformBuffer, 0, VK_WHOLE_SIZE};
    write[0].pBufferInfo = &bufferInfo;
    device.updateDescriptorSets(write);
}

void NeonReactorDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void NeonReactorDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void NeonReactorDemo::createRenderPipeline() {
    render.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("showcase.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayout(uniformDescriptorSetLayout)
            .name("neon_reactor")
        .build(render.layout);
}

void NeonReactorDemo::syncUniforms() {
    uniforms->resolutionTime = glm::vec4(
        static_cast<float>(swapChain.extent.width),
        static_cast<float>(swapChain.extent.height),
        animationTime,
        options.speed
    );
    uniforms->colorA = glm::vec4(options.primary, options.energy);
    uniforms->colorB = glm::vec4(options.accent, 1.0f);
    uniforms->controlsA = glm::vec4(options.swirl, options.density, options.glow, options.bloom);
    uniforms->controlsB = glm::vec4(options.pulse, options.grain, options.vignette, options.contrast);
}

void NeonReactorDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Neon Reactor");
    ImGui::SetWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    ImGui::Text("Procedural corridor showcase");
    ImGui::Separator();

    ImGui::Checkbox("Animate", &options.animate);
    ImGui::SliderFloat("Speed", &options.speed, 0.0f, 2.5f);
    ImGui::SliderFloat("Swirl", &options.swirl, 0.1f, 2.5f);
    ImGui::SliderFloat("Density", &options.density, 0.6f, 3.2f);
    ImGui::SliderFloat("Energy", &options.energy, 0.2f, 2.5f);
    ImGui::SliderFloat("Pulse", &options.pulse, 0.0f, 1.0f);
    ImGui::SliderFloat("Glow", &options.glow, 0.4f, 4.0f);
    ImGui::SliderFloat("Bloom", &options.bloom, 0.2f, 2.0f);
    ImGui::SliderFloat("Grain", &options.grain, 0.0f, 0.5f);
    ImGui::SliderFloat("Vignette", &options.vignette, 0.0f, 1.0f);
    ImGui::SliderFloat("Contrast", &options.contrast, 0.4f, 1.4f);
    ImGui::ColorEdit3("Primary", &options.primary.x);
    ImGui::ColorEdit3("Accent", &options.accent.x);

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void NeonReactorDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void NeonReactorDemo::onSwapChainRecreation() {
    createRenderPipeline();
    syncUniforms();
}

VkCommandBuffer* NeonReactorDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0.01f, 0.01f, 0.015f);

    renderToSwapChain([&] {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
        AppContext::renderClipSpaceQuad(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);
    return &commandBuffer;
}

void NeonReactorDemo::update(float time) {
    if (options.animate) {
        animationTime += time;
    }
    syncUniforms();
}

void NeonReactorDemo::cleanup() {
    AppContext::shutdown();
}

void NeonReactorDemo::onPause() {
    VulkanBaseApp::onPause();
}

int main() {
    try {
        fs::current_path("../../../../examples/");

        Settings settings;
        settings.width = 1600;
        settings.height = 900;
        settings.depthTest = false;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = NeonReactorDemo{settings};
        app.addPlugin(imGui);
        app.run();
    } catch (std::runtime_error& err) {
        spdlog::error(err.what());
    }
}
