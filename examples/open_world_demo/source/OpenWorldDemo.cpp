#include "OpenWorldDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "atmosphere/AtmosphereContants.hpp"

OpenWorldDemo::OpenWorldDemo(const Settings& settings) : VulkanBaseApp("Open World Demo", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/open_world_demo");
    fileManager.addSearchPathFront("../../examples/open_world_demo/data");
    fileManager.addSearchPathFront("../../examples/open_world_demo/spv");
    fileManager.addSearchPathFront("../../examples/open_world_demo/models");
    fileManager.addSearchPathFront("../../examples/open_world_demo/textures");
}

void OpenWorldDemo::initApp() {
    sceneGBuffer = std::make_shared<SceneGBuffer>();
    createDescriptorPool();
    initCamera();
    loadAtmosphereLUT();
    createSceneGBuffer();
    createSamplers();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    terrain = std::make_unique<Terrain>(device, descriptorPool, fileManager, swapChain.width(), swapChain.height(), renderPass, sceneGBuffer);
    skyDome = std::make_unique<SkyDome>(device, descriptorPool, fileManager, renderPass, swapChain.width(), swapChain.height());
//    shadowVolumeGenerator = std::make_unique<ShadowVolumeGenerator>(device, descriptorPool, fileManager, swapChain.width(), swapChain.height(), renderPass);
//    atmosphere = std::make_unique<AtmosphereG>(device, descriptorPool, fileManager, renderPass, swapChain.width(),
//                                              swapChain.height(), atmosphereLUT, terrain->gBuffer, shadowVolumeGenerator->shadowVolume);
//
//    clouds = std::make_unique<Clouds>(device, descriptorPool, fileManager, swapChain.width(), swapChain.height(), terrain->gBuffer, atmosphereLUT);

    terrain->renderTerrain();
//    shadowVolumeGenerator->initAdjacencyBuffers(terrain->vertexBuffer, *terrain->triangleCount);
}

void OpenWorldDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = sceneData.fieldOfView;
    cameraSettings.aspectRatio = float(width)/float(height);
    cameraSettings.zNear = sceneData.zNear;
    cameraSettings.zFar = sceneData.zFar;
    cameraSettings.acceleration = glm::vec3(60 * km);
    cameraSettings.velocity = glm::vec3(200 * km);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->lookAt(glm::vec3(-28 * km, 10 * km, 14 * km), glm::vec3(0, 0, 0), {0, 1, 0});
    camera->lookAt(glm::vec3(-3.4 * km, 1.2 * km, 13 * km), glm::vec3(0, 0, 0), {0, 1, 0});
//    auto target = EARTH_CENTER;
//    auto position = target + glm::vec3(0, 0, 1) * (EARTH_RADIUS);
//    camera->lookAt(position, target, glm::vec3(0, 1, 0));

//    FirstPersonSpectatorCameraSettings cameraSettings;
//    cameraSettings.fieldOfView = 90.0f;
//    cameraSettings.aspectRatio = float(width)/float(height);
//    cameraSettings.zNear = 0.01;
//    cameraSettings.zFar = 100000;
//    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
//    camera->lookAt(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), {0, 1, 0});
}

void OpenWorldDemo::loadAtmosphereLUT() {
    atmosphereLUT = std::make_shared<AtmosphereLookupTable>();
    auto data = loadFile(resource("atmosphere/irradiance.dat"));

    textures::create(device ,atmosphereLUT->irradiance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );
    device.setName<VK_OBJECT_TYPE_IMAGE>("atmosphere_irradiance_lut", atmosphereLUT->irradiance.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("atmosphere_irradiance_lut", atmosphereLUT->irradiance.imageView.handle);


    data = loadFile(resource("atmosphere/transmittance.dat"));
    textures::create(device ,atmosphereLUT->transmittance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );
    device.setName<VK_OBJECT_TYPE_IMAGE>("atmosphere_transmittance_lut", atmosphereLUT->transmittance.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("atmosphere_transmittance_lut", atmosphereLUT->transmittance.imageView.handle);

    data = loadFile(resource("atmosphere/scattering.dat"));
    textures::create(device ,atmosphereLUT->scattering, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );

    device.setName<VK_OBJECT_TYPE_IMAGE>("atmosphere_scatteringe_lut", atmosphereLUT->scattering.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("atmosphere_scattering_lut", atmosphereLUT->scattering.imageView.handle);

}


void OpenWorldDemo::createDescriptorPool() {
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

void OpenWorldDemo::createDescriptorSetLayouts() {
    atmosphereLUT->descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("atmosphere")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    atmosphereLUT->descriptorSet = descriptorPool.allocate({ atmosphereLUT->descriptorSetLayout }).front();

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = atmosphereLUT->descriptorSet ;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{atmosphereLUT-> irradiance.sampler.handle, atmosphereLUT-> irradiance.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &irradianceInfo;

    writes[1].dstSet = atmosphereLUT->descriptorSet ;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{atmosphereLUT-> transmittance.sampler.handle, atmosphereLUT-> transmittance.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = atmosphereLUT->descriptorSet ;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{atmosphereLUT-> scattering.sampler.handle, atmosphereLUT-> scattering.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &scatteringInfo;

    // single_mie_scattering
    writes[3] = writes[2];
    writes[3].dstBinding = 3;

    device.updateDescriptorSets(writes);

    sceneGBuffer->descriptorSetLayout =
	device.descriptorSetLayoutBuilder()
		.name("g_buffer")
		.binding(0)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(1)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(2)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(3)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(4)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(5)
			.descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
			.immutableSamplers(samplers->nearest)
		.binding(6)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.binding(7)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.binding(8)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.binding(9)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.binding(10)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.binding(11)
			.descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
			.descriptorCount(1)
			.shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
		.createLayout();
}

void OpenWorldDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { sceneGBuffer->descriptorSetLayout });
    sceneGBuffer->descriptorSet = sets[0];
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("scene_g_buffer_ds", sceneGBuffer->descriptorSet);

	auto writes = initializers::writeDescriptorSets<6>();

    writes[0].dstSet = sceneGBuffer->descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo gPositionInfo{VK_NULL_HANDLE, sceneGBuffer->position.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &gPositionInfo;

    writes[1].dstSet = sceneGBuffer->descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo gNormalInfo{VK_NULL_HANDLE, sceneGBuffer->normal.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &gNormalInfo;

    writes[2].dstSet = sceneGBuffer->descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo gAlbedoInfo{VK_NULL_HANDLE, sceneGBuffer->albedo.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &gAlbedoInfo;

    writes[3].dstSet = sceneGBuffer->descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo gMaterialInfo{VK_NULL_HANDLE, sceneGBuffer->material.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &gMaterialInfo;

    writes[4].dstSet = sceneGBuffer->descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo gDepthInfo{VK_NULL_HANDLE, sceneGBuffer->depth.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[4].pImageInfo = &gDepthInfo;

    writes[5].dstSet = sceneGBuffer->descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo gObjectTypetInfo{VK_NULL_HANDLE, sceneGBuffer->objectType.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[5].pImageInfo = &gObjectTypetInfo;

    device.updateDescriptorSets(writes);

    // storage image descriptors
    writes[0].dstBinding = 6;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[1].dstBinding = 7;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    gNormalInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    writes[2].dstBinding = 8;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[3].dstBinding = 9;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[4].dstBinding = 10;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    writes[5].dstBinding = 11;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

    device.updateDescriptorSets(writes);


}

void OpenWorldDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OpenWorldDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OpenWorldDemo::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    render.pipeline =
        builder
            .shaderStage()
                .vertexShader("../../data/shaders/pass_through.vert.spv")
                .fragmentShader("../../data/shaders/pass_through.frag.spv")
            .vertexInputState()
                .addVertexBindingDescriptions(Vertex::bindingDisc())
                .addVertexAttributeDescriptions(Vertex::attributeDisc())
            .inputAssemblyState()
                .triangles()
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
                    .add()
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(render.layout);
    //    @formatter:on
}

void OpenWorldDemo::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void OpenWorldDemo::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
}

void OpenWorldDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
    terrain->resize(renderPass, width, height);
//    shadowVolumeGenerator->resize(renderPass, width, height);
//    atmosphere->resize(renderPass, terrain->gBuffer, shadowVolumeGenerator->shadowVolume, width, height);
    skyDome->resize(renderPass, width, height);
}

VkCommandBuffer *OpenWorldDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    if(terrain->debugMode){
        skyDome->render(commandBuffer);
        terrain->render(commandBuffer);
//        shadowVolumeGenerator->render(commandBuffer);
    }else{
//        atmosphere->render(commandBuffer);
    }

    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OpenWorldDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Open world");
    ImGui::SetWindowSize({0, 0});

    ImGui::Text("Camera:");
    ImGui::Indent(16);
    ImGui::SliderFloat("FOV", &sceneData.fieldOfView, 5, 120);
    ImGui::Indent(-16);

    ImGui::Text("Sun:");
    ImGui::Indent(16);
    ImGui::SliderFloat("Sun Azimuth", &sceneData.sun.azimuth, 0, 360);
    ImGui::SliderFloat("Sun Elevation", &sceneData.sun.elevation, 0, 326);
    ImGui::Indent(-16);

    static float exposureScale = 0.5;
    if(ImGui::SliderFloat("exposure", &exposureScale, 0, 1)){
        float power = remap(exposureScale, 0, 1, -20, 20);
        sceneData.exposure = 10.f * glm::pow(1.1f, power);
    }

    static bool showTerrain = false;
    ImGui::Text("Systems:");
    ImGui::Indent(16);
    ImGui::Checkbox("Terrain", &showTerrain);
    ImGui::Indent(-16);

    ImGui::Checkbox("light shaft", &sceneData.enableLightShaft);
    ImGui::End();

    if(showTerrain){
        terrain->renderUI();
    }

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void OpenWorldDemo::update(float time) {

    static bool onGround = false;
    auto &v = sceneData.cameraVelocity;

//    if(!onGround) {
//        auto acceleration = gravity;
//        v += acceleration * time;
//        camera->move(v.x, v.y, v.z);
//    }


    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    camera->fieldOfView(sceneData.fieldOfView);
    auto cam = camera->cam();

    updateScene(time);
    terrain->update(sceneData);
//    clouds->update(sceneData);
    skyDome->update(sceneData);
//    atmosphere->update(sceneData);
//    shadowVolumeGenerator->update(sceneData);

//    glm::vec3 contactPoint;
//    static auto groundOffset = 2 * meter;
//    if(terrain->collidesWithCamera(contactPoint)){
//        camera->position(contactPoint - normalize(v) * groundOffset);
//        onGround = true;
//    }

    auto msg = fmt::format("{} - FPS {}, position: {}", title, framePerSecond, sceneData.eyes);
    glfwSetWindowTitle(window, msg.c_str());

}

void OpenWorldDemo::updateScene(float time) {
    sceneData.camera = camera->cam();
    sceneData.eyes = camera->position();
    sceneData.time = time;

    auto theta = glm::radians(sceneData.sun.azimuth);
    auto phi = glm::radians(sceneData.sun.elevation);

    auto axis = glm::angleAxis(phi, glm::vec3{0, 0, 1}) * glm::angleAxis(theta, glm::vec3(0, 1, 0));
    sceneData.sun.position = glm::normalize(axis)  * glm::vec3(SUN_DISTANCE, 0, 0);

}

void OpenWorldDemo::checkAppInputs() {
    camera->processInput();
}

void OpenWorldDemo::cleanup() {
    VulkanBaseApp::cleanup();
}

void OpenWorldDemo::onPause() {
    VulkanBaseApp::onPause();
}

void OpenWorldDemo::newFrame() {
    terrain->renderTerrain();
//    clouds->renderClouds();
//    shadowVolumeGenerator->generate(sceneData, terrain->vertexBuffer, *terrain->triangleCount);
}

void OpenWorldDemo::createSceneGBuffer() {
    VkImageCreateInfo info = initializers::imageCreateInfo(
            VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
            width, height);

    VkImageSubresourceRange subresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    sceneGBuffer->position.image = device.createImage(info);
    sceneGBuffer->position.imageView = sceneGBuffer->position.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    sceneGBuffer->normal.image = device.createImage(info);
    sceneGBuffer->normal.imageView = sceneGBuffer->normal.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    sceneGBuffer->albedo.image = device.createImage(info);
    sceneGBuffer->albedo.imageView = sceneGBuffer->albedo.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    sceneGBuffer->material.image = device.createImage(info);
    sceneGBuffer->material.imageView = sceneGBuffer->material.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);


    info.format = VK_FORMAT_R8_UINT;
    sceneGBuffer->objectType.image = device.createImage(info);
    sceneGBuffer->objectType.imageView = sceneGBuffer->objectType.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    info.format = VK_FORMAT_R32_SFLOAT;
    sceneGBuffer->depth.image = device.createImage(info);
    sceneGBuffer->depth.imageView = sceneGBuffer->depth.image.createView(
            info.format, VK_IMAGE_VIEW_TYPE_2D, subresourceRange);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){

        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        sceneGBuffer->position.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                 subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        sceneGBuffer->normal.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                               subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        sceneGBuffer->albedo.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                               subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        sceneGBuffer->material.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                 subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);


        sceneGBuffer->objectType.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                 subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

        sceneGBuffer->depth.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL,
                                                   subresourceRange, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT,
                                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
}

void OpenWorldDemo::createSamplers() {
    samplers = std::make_shared<Samplers>();
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;

    samplers->nearest = device.createSampler(samplerInfo);
}


int main(){
    try{

        Settings settings;
//        settings.fullscreen = true;
        settings.screen = 1;
        settings.depthTest = true;
        settings.enabledFeatures.tessellationShader = VK_TRUE;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.geometryShader = VK_TRUE;
        settings.enabledFeatures.vertexPipelineStoresAndAtomics = VK_TRUE;
        settings.enabledFeatures.depthClamp = VK_TRUE;

        auto app = OpenWorldDemo{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}