#include "OrbitalCathedralDemo.hpp"

#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"

namespace {
    constexpr float TAU = 6.28318530718f;
}

OrbitalCathedralDemo::OrbitalCathedralDemo(const Settings& settings)
    : VulkanBaseApp("Orbital Cathedral", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("orbital_cathedral");
    fileManager().addSearchPathFront("orbital_cathedral/data");
    fileManager().addSearchPathFront("orbital_cathedral/spv");
    fileManager().addSearchPathFront("orbital_cathedral/models");
    fileManager().addSearchPathFront("orbital_cathedral/textures");
}

void OrbitalCathedralDemo::initApp() {
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createCamera();
    createBuffers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    syncUniforms();
    updateInstances();
}

void OrbitalCathedralDemo::beforeDeviceCreation() {
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

void OrbitalCathedralDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 64;
    std::array<VkDescriptorPoolSize, 2> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxSets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxSets}
    }};
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void OrbitalCathedralDemo::createCamera() {
    OrbitingCameraSettings settings{};
    settings.offsetDistance = 22.0f;
    settings.rotationSpeed = 0.18f;
    settings.modelHeight = 0.0f;
    settings.fieldOfView = 45.0f;
    settings.aspectRatio = static_cast<float>(swapChain.extent.width) / static_cast<float>(swapChain.extent.height);
    settings.zNear = 0.1f;
    settings.zFar = 150.0f;
    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), settings);
}

void OrbitalCathedralDemo::createBuffers() {
    instanceMatrices.resize(kInstanceCount, glm::mat4{1.0f});
    instanceBuffer = device.createCpuVisibleBuffer(instanceMatrices.data(), BYTE_SIZE(instanceMatrices), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    MaterialData defaults{};
    materialBuffer = device.createCpuVisibleBuffer(&defaults, sizeof(defaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    material = reinterpret_cast<MaterialData*>(materialBuffer.map());
}

void OrbitalCathedralDemo::createDescriptorSetLayouts() {
    materialSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("orbital_cathedral_material")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void OrbitalCathedralDemo::updateDescriptorSets() {
    instanceSet = AppContext::allocateInstanceDescriptorSet();
    materialSet = descriptorPool.allocate({materialSetLayout}).front();

    auto writes = initializers::writeDescriptorSets<2>();

    writes[0].dstSet = instanceSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo instanceInfo{instanceBuffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &instanceInfo;

    writes[1].dstSet = materialSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo materialInfo{materialBuffer, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &materialInfo;

    device.updateDescriptorSets(writes);
}

void OrbitalCathedralDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OrbitalCathedralDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void OrbitalCathedralDemo::createRenderPipeline() {
    render.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("cathedral.vert.spv"))
                .fragmentShader(resource("cathedral.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayout(AppContext::instanceSetLayout())
                .addDescriptorSetLayout(materialSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera))
            .name("orbital_cathedral")
        .build(render.layout);
}

void OrbitalCathedralDemo::updateInstances() {
    const float time = animationTime * options.speed;
    const float height = options.span;
    const float halfHeight = height * 0.5f;
    const float baseRadius = options.radius;
    const float pulse = 1.0f + 0.25f * options.pulse * std::sin(time * 2.4f);

    uint32_t index = 0;
    for (uint32_t layer = 0; layer < kLayers; ++layer) {
        const float v = static_cast<float>(layer) / static_cast<float>(kLayers - 1);
        const float centered = v * 2.0f - 1.0f;
        const float y = centered * halfHeight;
        const float arch = std::pow(std::abs(centered), 1.35f);
        const float ringRadius = baseRadius + arch * 2.8f + 0.45f * std::sin(v * 16.0f + time * 1.6f) * options.pulse;

        for (uint32_t spoke = 0; spoke < kSpokes; ++spoke) {
            const float u = static_cast<float>(spoke) / static_cast<float>(kSpokes);
            const float angle = u * TAU + centered * options.twist + time * (0.18f + 0.04f * std::sin(v * 8.0f));
            const float radialBreath = 0.4f * std::sin(v * 18.0f + u * 12.0f + time * 2.1f);
            const float x = std::cos(angle) * (ringRadius + radialBreath);
            const float z = std::sin(angle) * (ringRadius + radialBreath);
            const float localLift = 0.55f * std::sin(u * TAU * 3.0f + time * 1.7f + v * 10.0f) * options.lift;

            glm::mat4 model{1.0f};
            model = glm::translate(model, glm::vec3{x, y + localLift, z});
            model = glm::rotate(model, angle + time * 0.2f, glm::vec3{0.0f, 1.0f, 0.0f});
            model = glm::rotate(model, 0.65f * std::sin(v * 7.0f + time + u * TAU), glm::vec3{0.0f, 0.0f, 1.0f});

            const float thickness = 0.10f + 0.05f * (0.5f + 0.5f * std::sin(v * 24.0f + u * 5.0f + time * 2.0f));
            const float length = (0.55f + 1.45f * (1.0f - arch)) * pulse;
            model = glm::scale(model, glm::vec3{thickness, length, thickness * 1.6f});
            instanceMatrices[index++] = model;
        }
    }

    for (uint32_t i = 0; i < kSpineCount; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(kSpineCount - 1);
        const float centered = v * 2.0f - 1.0f;
        const float y = centered * (halfHeight - 0.8f);
        const float r = 0.65f + 0.25f * std::sin(time * 2.0f + v * 14.0f);
        const float angle = time * 0.8f + v * TAU * 2.0f;

        glm::mat4 model{1.0f};
        model = glm::translate(model, glm::vec3{std::cos(angle) * r, y, std::sin(angle) * r});
        model = glm::rotate(model, angle * 1.3f, glm::vec3{0.0f, 1.0f, 0.0f});
        model = glm::rotate(model, time + v * 8.0f, glm::vec3{1.0f, 0.0f, 0.0f});
        const float scale = 0.22f + 0.12f * std::sin(time * 3.0f + v * 10.0f) * options.pulse;
        model = glm::scale(model, glm::vec3{scale, 0.8f + scale * 2.4f, scale});
        instanceMatrices[index++] = model;
    }

    instanceBuffer.copy(instanceMatrices.data(), BYTE_SIZE(instanceMatrices));
}

void OrbitalCathedralDemo::syncUniforms() {
    material->colorA = glm::vec4(options.primary, options.glow);
    material->colorB = glm::vec4(options.accent, 0.95f);
    material->lightDirTime = glm::vec4(glm::normalize(glm::vec3{0.45f, 0.8f, -0.35f}), animationTime);
    material->controls = glm::vec4(options.glow, options.banding, options.shimmer, options.contrast);
    AppContext::updateSunDirection(glm::normalize(glm::vec3{0.45f, 0.7f, -0.2f}));
}

void OrbitalCathedralDemo::renderCathedral(VkCommandBuffer commandBuffer) {
    std::array<VkDescriptorSet, 2> sets{instanceSet, materialSet};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
    camera->push(commandBuffer, render.layout);
    AppContext::drawCube(commandBuffer, kInstanceCount);
}

void OrbitalCathedralDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Orbital Cathedral");
    ImGui::SetWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    ImGui::Text("Instanced architectural sculpture");
    ImGui::Separator();
    ImGui::Checkbox("Animate", &options.animate);
    ImGui::SliderFloat("Speed", &options.speed, 0.0f, 2.0f);
    ImGui::SliderFloat("Twist", &options.twist, 0.0f, 4.5f);
    ImGui::SliderFloat("Radius", &options.radius, 2.5f, 7.5f);
    ImGui::SliderFloat("Span", &options.span, 8.0f, 22.0f);
    ImGui::SliderFloat("Pulse", &options.pulse, 0.0f, 1.2f);
    ImGui::SliderFloat("Lift", &options.lift, 0.0f, 2.0f);
    ImGui::SliderFloat("Glow", &options.glow, 0.2f, 2.5f);
    ImGui::SliderFloat("Banding", &options.banding, 0.5f, 6.0f);
    ImGui::SliderFloat("Shimmer", &options.shimmer, 0.0f, 8.0f);
    ImGui::SliderFloat("Contrast", &options.contrast, 0.3f, 1.4f);
    ImGui::ColorEdit3("Primary", &options.primary.x);
    ImGui::ColorEdit3("Accent", &options.accent.x);
    ImGui::Text("%u instances", kInstanceCount);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void OrbitalCathedralDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void OrbitalCathedralDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    createRenderPipeline();
    syncUniforms();
}

VkCommandBuffer* OrbitalCathedralDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0.03f, 0.04f, 0.06f);

    renderToSwapChain([&] {
        AppContext::renderAtmosphere(commandBuffer, *camera);
        AppContext::renderFloor(commandBuffer, *camera);
        renderCathedral(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);
    return &commandBuffer;
}

void OrbitalCathedralDemo::update(float time) {
    if (!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    if (options.animate) {
        animationTime += time;
    }
    syncUniforms();
    updateInstances();
}

void OrbitalCathedralDemo::checkAppInputs() {
    camera->processInput();
}

void OrbitalCathedralDemo::cleanup() {
    AppContext::shutdown();
}

void OrbitalCathedralDemo::onPause() {
    VulkanBaseApp::onPause();
}

int main() {
    try {
        fs::current_path("../../../../examples/");

        Settings settings;
        settings.width = 1600;
        settings.height = 960;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = OrbitalCathedralDemo{settings};
        app.addPlugin(imGui);
        app.run();
    } catch (std::runtime_error& err) {
        spdlog::error(err.what());
    }
}
