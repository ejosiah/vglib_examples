#include "VolumetricFog.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

VolumetricFog::VolumetricFog(const Settings& settings) : VulkanBaseApp("Volumetric fog", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/volumetric_fog");
    fileManager.addSearchPathFront("../../examples/volumetric_fog/data");
    fileManager.addSearchPathFront("../../examples/volumetric_fog/spv");
    fileManager.addSearchPathFront("../../examples/volumetric_fog/models");
    fileManager.addSearchPathFront("../../examples/volumetric_fog/textures");

    static VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    deviceCreateNextChain = &dynamicRenderingFeatures;
}

void VolumetricFog::initApp() {
    createDescriptorPool();
    loadModel();
    initCamera();
    initScene();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initSky();
    initShadowMap();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();

}

void VolumetricFog::initSky() {
    sky = Sky{&fileManager, &device, &descriptorPool, &renderPass, {width, height}, &camera->camera, &m_scene};
    sky.init();
}

void VolumetricFog::initShadowMap() {
    m_shadowMap = ShadowMap{ &fileManager, &device, &descriptorPool, &m_scene };
    m_shadowMap.init();
}

void VolumetricFog::loadModel() {
    phong::load(fileManager.getFullPath("Sponza/sponza.obj")->string(), device, descriptorPool, m_sponza);
    spdlog::info("Sponza bounds: min: {}, max: {}", m_sponza.bounds.min, m_sponza.bounds.max);
}

void VolumetricFog::initScene() {
    constexpr float SunAngularRadius = 0.004675;
    m_scene.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneData), "scene");
    m_scene.cpu = reinterpret_cast<SceneData*>(m_scene.gpu.map());
    m_scene.cpu->earthCenter = {0, - 6360 * km, 0};
    m_scene.cpu->sunSize = {glm::tan(SunAngularRadius), glm::cos(SunAngularRadius), 0};
    m_scene.cpu->camera = glm::vec3(0);
    m_scene.cpu->sunDirection = glm::normalize(glm::vec3(1));
    m_scene.cpu->exposure = 10.f;
    m_scene.camera = &camera->camera;
    m_scene.zNear = camera->znear;
    m_scene.zFar = camera->zfar;
    m_scene.bounds = std::make_tuple(m_sponza.bounds.min, m_sponza.bounds.max);
}

void VolumetricFog::initCamera() {
//    OrbitingCameraSettings cameraSettings;
    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.orbitMinZoom = 0.1;
//    cameraSettings.orbitMaxZoom = 512.0f;
//    cameraSettings.offsetDistance = 1.0f;
//    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.acceleration = glm::vec3(1 * m);
    cameraSettings.velocity = glm::vec3(5 * m);
    cameraSettings.zFar = 50 * m;

//    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    auto position = (m_sponza.bounds.max + m_sponza.bounds.min) * 0.5f;
    spdlog::info("camera position: {}", position);
    camera->position(position);
}


void VolumetricFog::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 8> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void VolumetricFog::createDescriptorSetLayouts() {
    m_scene.sceneSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("scene_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    m_scene.sceneSet = descriptorPool.allocate({ m_scene.sceneSetLayout }).front();
}

void VolumetricFog::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<1>();

    writes[0].dstSet = m_scene.sceneSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo sceneInfo{m_scene.gpu.buffer, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &sceneInfo;

    device.updateDescriptorSets(writes);
}

void VolumetricFog::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumetricFog::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void VolumetricFog::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(sky.descriptor().uboDescriptorSetLayout)
                    .addDescriptorSetLayout(sky.descriptor().lutDescriptorSetLayout)
                    .addDescriptorSetLayout(m_sponza.descriptorSetLayout)
                    .addDescriptorSetLayout(m_scene.sceneSetLayout)
                    .addDescriptorSetLayout(m_shadowMap.lightMatrixDescriptorSetLayout)
                    .addDescriptorSetLayout(m_shadowMap.shadowMapDescriptorSetLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}

void VolumetricFog::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void VolumetricFog::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
}

void VolumetricFog::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
    sky.reload(&renderPass, width, height);
}

VkCommandBuffer *VolumetricFog::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    sky.render(commandBuffer);

    static std::array<VkDescriptorSet, 6> sets;
    sets[0] = sky.descriptor().uboDescriptorSet;
    sets[1] = sky.descriptor().lutDescriptorSet;
    sets[3] = m_scene.sceneSet;
    sets[4] = m_shadowMap.lightMatrixDescriptorSet;
    sets[5] = m_shadowMap.shadowMapDescriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout);

    auto numPrims = m_sponza.meshes.size();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_sponza.vertexBuffer.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_sponza.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (auto i = 0; i < numPrims; i++) {
        sets[2] = m_sponza.meshes[i].material.descriptorSet;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        m_sponza.meshes[i].drawIndexed(commandBuffer);
    }

    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumetricFog::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Scene");
    ImGui::SetWindowSize({0, 0});

    ImGui::Text("sun:");
    ImGui::Indent(16);
    ImGui::SliderFloat("zenith", &sun.zenith, -20, 90);
    ImGui::SliderFloat("azimuth", &sun.azimuth, 0, 360);
    ImGui::Indent(-16);

    static float exposureScale = 0.5;
    if(ImGui::SliderFloat("exposure", &exposureScale, 0, 1)){
        float power = remap(exposureScale, 0, 1, -20, 20);
        m_scene.cpu->exposure = 10.f * glm::pow(1.1f, power);
    }

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VolumetricFog::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    updateSunDirection();
    sky.update(time);
    castShadow();

    auto wTitle = fmt::format("{} - camera: {:f}", title, camera->position());
    glfwSetWindowTitle(window, wTitle.c_str());
}

void VolumetricFog::checkAppInputs() {
    camera->processInput();
}

void VolumetricFog::cleanup() {
    VulkanBaseApp::cleanup();
}

void VolumetricFog::onPause() {
    VulkanBaseApp::onPause();
}

void VolumetricFog::updateSunDirection() {
    glm::vec3 p{1, 0, 0};
    auto axis = glm::angleAxis(glm::radians(sun.zenith), glm::vec3{0, 0, 1});
    p = axis * p;

    axis = glm::angleAxis(glm::radians(sun.azimuth), glm::vec3{0, 1, 0});
    m_scene.cpu->sunDirection  = glm::normalize(axis * p);
    m_scene.cpu->camera = camera->position();
}

void VolumetricFog::castShadow() {
    m_shadowMap.generate([this](auto commandBuffer){
       m_sponza.draw(commandBuffer);
    });
}


int main(){
    try{

        Settings settings;
        settings.depthTest = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

        auto app = VolumetricFog{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}