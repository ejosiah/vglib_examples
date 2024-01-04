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
    Texture texture;
    createDescriptorPool();
    loadModel();
    initCamera();
    initScene();
    initFog();
    createFogTextures();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    updateFogDescriptorSets();
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

void VolumetricFog::initFog() {
    m_fog.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(FogData), "fog_ubo");
    m_fog.cpu = reinterpret_cast<FogData*>(m_fog.gpu.map());

    m_fog.cpu->froxelDim = glm::ivec3(128);
    m_fog.cpu->scatteringFactor = 0.1;
    m_fog.cpu->constantDensity = 0.1;
    m_fog.cpu->heightFogDensity = 0;
    m_fog.cpu->heightFogFalloff = 1.0;
    m_fog.cpu->g = 0;
}

void VolumetricFog::createFogTextures() {
    textures::create(device, m_fog.fogData, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, 128}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    textures::create(device, m_fog.lightContribution, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, 128}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    textures::create(device, m_fog.integratedScattering, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, 128}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
}

void VolumetricFog::initShadowMap() {
    m_shadowMap = ShadowMap{ &fileManager, &device, &descriptorPool, &m_scene };
    m_shadowMap.init();
}

void VolumetricFog::loadModel() {
//    phong::load(fileManager.getFullPath("Sponza/sponza.obj")->string(), device, descriptorPool, m_sponza);
    phong::load(R"(C:\Users\Josiah Ebhomenye\source\repos\VolumetricLighting\bin\Debug\meshes\sponza.obj)", device, descriptorPool, m_sponza, {}, false, 1, centimetre);
    spdlog::info("Sponza bounds: min: {}, max: {}", m_sponza.bounds.min, m_sponza.bounds.max);
}

void VolumetricFog::initScene() {
    constexpr float SunAngularRadius = 0.004675;
    m_scene.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneData), "scene");
    m_scene.cpu = reinterpret_cast<SceneData*>(m_scene.gpu.map());
    m_scene.cpu->earthCenter = {0, - 6360 * km, 0};
    m_scene.cpu->sunSize = {glm::tan(SunAngularRadius), glm::cos(SunAngularRadius), 0};
    m_scene.cpu->camera = glm::vec3(0);
    m_scene.cpu->cameraDirection = glm::vec3(0, 0, -1);
    m_scene.cpu->sunDirection = glm::normalize(glm::vec3(1));
    m_scene.cpu->whitePoint = glm::vec3(1);
    m_scene.cpu->exposure = 10.f;
    m_scene.cpu->znear = camera->znear;
    m_scene.cpu->zfar = camera->zfar;
    m_scene.cpu->viewProjection = camera->camera.proj * camera->camera.view * camera->camera.model;
    m_scene.cpu->inverseViewProjection = glm::inverse(m_scene.cpu->viewProjection);
    m_scene.camera = &camera->camera;
    m_scene.zNear = camera->znear;
    m_scene.zFar = camera->zfar;
    m_scene.bounds = std::make_tuple(m_sponza.bounds.min, m_sponza.bounds.max);

    camera->camera.proj * glm::vec4(2, 3, 4, 1);
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
    cameraSettings.zFar = 200 * m;
    cameraSettings.zNear = 1 * cm;

//    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    auto position = (m_sponza.bounds.max + m_sponza.bounds.min) * 0.5f;
    spdlog::info("camera position: {}", position);
    camera->position(position);
}


void VolumetricFog::createDescriptorPool() {
    constexpr uint32_t maxSets = 400;
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

    m_fog.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fog_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(6)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    auto sets = descriptorPool.allocate({ m_scene.sceneSetLayout, m_fog.descriptorSetLayout });

    m_scene.sceneSet = sets[0];
    m_fog.descriptorSet = sets[1];

}

void VolumetricFog::updateFogDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<7>();
    
    writes[0].dstSet = m_fog.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ m_fog.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;
    
    
    writes[1].dstSet = m_fog.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo fogDataInfo{m_fog.fogData.sampler.handle, m_fog.fogData.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &fogDataInfo;
    
    writes[2].dstSet = m_fog.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo lightContribInfo{m_fog.lightContribution.sampler.handle, m_fog.lightContribution.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &lightContribInfo;
    
    writes[3].dstSet = m_fog.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{m_fog.integratedScattering.sampler.handle, m_fog.integratedScattering.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &scatteringInfo;
    
    writes[4].dstSet = m_fog.descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &fogDataInfo;
    
    writes[5].dstSet = m_fog.descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &lightContribInfo;
    
    writes[6].dstSet = m_fog.descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    writes[6].pImageInfo = &scatteringInfo;
    
    device.updateDescriptorSets(writes);
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
                .allowDerivatives()
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

        rayMarch.pipeline =
            builder
                .basePipeline(render.pipeline)
                .shaderStage()
                    .fragmentShader(resource("ray_march.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(m_fog.descriptorSetLayout)
                .name("render_ray_march")
                .build(rayMarch.layout);
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
    renderWithRayMarching(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumetricFog::renderWithVolumeTextures(VkCommandBuffer commandBuffer) {
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
}

void VolumetricFog::renderWithRayMarching(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 7> sets;
    sets[0] = sky.descriptor().uboDescriptorSet;
    sets[1] = sky.descriptor().lutDescriptorSet;
    sets[3] = m_scene.sceneSet;
    sets[4] = m_shadowMap.lightMatrixDescriptorSet;
    sets[5] = m_shadowMap.shadowMapDescriptorSet;
    sets[6] = m_fog.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.pipeline.handle);
    camera->push(commandBuffer, rayMarch.layout);

    auto numPrims = m_sponza.meshes.size();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_sponza.vertexBuffer.buffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_sponza.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    for (auto i = 0; i < numPrims; i++) {
        sets[2] = m_sponza.meshes[i].material.descriptorSet;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
        m_sponza.meshes[i].drawIndexed(commandBuffer);
    }
}

void VolumetricFog::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Scene");
    ImGui::SetWindowSize({500, 400});

    if(ImGui::CollapsingHeader("sun")) {
        ImGui::SliderFloat("zenith", &sun.zenith, -20, 90);
        ImGui::SliderFloat("azimuth", &sun.azimuth, 0, 360);
    }

    if(ImGui::CollapsingHeader("Fog")){
        ImGui::SliderFloat("Constant Density", &m_fog.cpu->constantDensity, 0, 1);
        ImGui::SliderFloat("scattering factor", &m_fog.cpu->scatteringFactor, 0, 1);
        ImGui::SliderFloat("Height Fog Density", &m_fog.cpu->heightFogDensity, 0, 10);
        ImGui::SliderFloat("Height Fog falloff", &m_fog.cpu->heightFogFalloff, 0, 10);
        ImGui::SliderFloat("Anisotropy", &m_fog.cpu->g, -0.9999, 0.9999);
    }

    if(ImGui::CollapsingHeader("post processing")) {
        static float exposureScale = 0.5;
        if (ImGui::SliderFloat("exposure", &exposureScale, 0, 1)) {
            float power = remap(exposureScale, 0.f, 1.f, -20.f, 20.f);
            m_scene.cpu->exposure = 10.f * glm::pow(1.1f, power);
        }
    }

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void VolumetricFog::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
        m_scene.cpu->viewProjection = camera->camera.proj * camera->camera.view;
        m_scene.cpu->inverseViewProjection = glm::inverse(m_scene.cpu->viewProjection);
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
    m_scene.cpu->cameraDirection = camera->viewDir;
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