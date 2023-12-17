#include "AtmosphericScattering2.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "dft.hpp"

AtmosphericScattering2::AtmosphericScattering2(const Settings& settings) : VulkanBaseApp("atmospheric scattering", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering2");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering2/data");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering2/spv");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering2/models");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering2/textures");
}

void AtmosphericScattering2::initApp() {
    initCamera();
    createDescriptorPool();
    atmosphereGenerator = std::make_unique<AtmosphereGenerator>(&device, &descriptorPool, &fileManager);
    atmosphereGenerator->generateLUT();
    atmosphere = const_cast<AtmosphereDescriptor*>(&atmosphereGenerator->atmosphereDescriptor());
    initUBO();
    createTextures();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
}

void AtmosphericScattering2::initUBO() {
    uboBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Ubo), "uniforms");
    ubo = reinterpret_cast<Ubo*>(uboBuffer.map());

    ubo->white_point = glm::vec3(1);
    ubo->earth_center = {0, -6360000, 0};
    auto kSunAngularRadius = atmosphereGenerator->params.sunAngularRadius;
    ubo->sun_size = glm::vec3(glm::tan(kSunAngularRadius), glm::cos(kSunAngularRadius), 0);
    ubo->sphereAlbedo = glm::vec3(0.8);
    ubo->groundAlbedo = {0.0, 0.0, 0.04};
    ubo->sun_direction = glm::normalize(glm::vec3(1));
    ubo->near = Z_NEAR;
    ubo->far = Z_FAR;
    ubo->frame = 0;

    auto quad = ClipSpace::Quad::positions;
    std::vector<glm::vec4> positions;
    for(int i = 0; i < quad.size(); i+= 2){
        glm::vec4 p{quad[i], 0, 1};
        positions.push_back(p);
    }

    screenBuffer = device.createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void AtmosphericScattering2::createTextures() {
    textures::create(device, atmosphereVolume.transmittance, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
            , atmosphereVolume.size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    textures::create(device, atmosphereVolume.inScattering, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT
            , atmosphereVolume.size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float ));

    atmosphereVolume.transmittance.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    atmosphereVolume.inScattering.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void AtmosphericScattering2::createCameraVolume() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        static std::array<VkDescriptorSet, 4> sets;
        sets[0] = atmosphere->uboDescriptorSet;
        sets[1] = atmosphere->lutDescriptorSet;
        sets[2] = uboSet;
        sets[3] = cameraVolumeSet;
        auto dim = atmosphereVolume.size;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cameraVolume.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cameraVolume.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, dim.x, dim.y, dim.z);
    });
}

void AtmosphericScattering2::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 90;
    cameraSettings.aspectRatio = float(width)/float(height);
    cameraSettings.horizontalFov = true;
    cameraSettings.zNear = Z_NEAR;
    cameraSettings.zFar = Z_FAR;
    cameraSettings.acceleration = glm::vec3(20 * km);
    cameraSettings.velocity = glm::vec3(80* km);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
   camera->lookAt(glm::vec3(0, 1 * km, 10 * km), glm::vec3(0, 1 * km, 0), {0, 1, 0});
//   camera->lookAt(glm::vec3(0, 0, 1), glm::vec3(0, 0, 0), {0, 1, 0});
   //camera->lookAt(glm::vec3(0, 0, 1), glm::vec3(0), {0, 1, 0});

   auto linearZ = [](auto z) {
       return ((Z_NEAR * Z_FAR) / (z * (Z_FAR - Z_NEAR) - Z_FAR));
   };

   auto projection = camera->cam().proj;
//   spdlog::info("{}", glm::column(projection, 0));
//   spdlog::info("{}", glm::column(projection, 1));
//   spdlog::info("{}", glm::column(projection, 2));
//   spdlog::info("{}", glm::column(projection, 3));

   glm::vec4 pos{2, 4, 28 * km, 1};
   auto res = projection * pos;
   res /= res.w;
   spdlog::info("projection: {}", res);

   auto f = Z_FAR;
   auto n = Z_NEAR;
   float z = pos.z;
   auto cz = (f * (z + n))/(-z * (n-f));
   spdlog::info("clip z: {}", cz);

//   glm::vec4 clip{-0.9688, -0.9688,	0.0156, 1.0};
//   spdlog::info("z: {}", linearZ(clip.z));
//   auto world = glm::inverse(camera->cam().proj * camera->cam().view) * clip;
////   world /= world.w;
//   spdlog::info("clip0: {}, world0: {}", clip, world);
//
//   clip = glm::vec4{-0.9688, -0.9688, 0.4844, 1};
//   spdlog::info("z: {}", linearZ(clip.z));
//    world = glm::inverse(camera->cam().proj * camera->cam().view) * clip;
////    world /= world.w;
//    spdlog::info("clip0: {}, world0: {}", clip, world);
//
//    clip = glm::vec4{-0.9688, -0.9688, 0.9844, 1};
//   spdlog::info("z: {}", linearZ(clip.z));
//    world = glm::inverse(camera->cam().proj * camera->cam().view) * clip;
////    world /= world.w;
//    spdlog::info("clip0: {}, world0: {}", clip, world);

}


void AtmosphericScattering2::createDescriptorPool() {
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

void AtmosphericScattering2::createDescriptorSetLayouts() {
    uboSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ubo")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .createLayout();

    cameraVolumeSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("camera_volume_set_layout_image")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    atmosphereVolumeSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("camera_volume_set_layout_texture")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

}

void AtmosphericScattering2::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ uboSetLayout, cameraVolumeSetLayout, atmosphereVolumeSetLayout });
    uboSet = sets[0];
    cameraVolumeSet = sets[1];
    atmosphereVolumeSet = sets[2];

    auto writes = initializers::writeDescriptorSets<5>();

    writes[0].dstSet = uboSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE };
    writes[0].pBufferInfo = &uboInfo;

    writes[1].dstSet = cameraVolumeSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{atmosphereVolume.transmittance.sampler.handle, atmosphereVolume.transmittance.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = cameraVolumeSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo inScatteringInfo{atmosphereVolume.inScattering.sampler.handle, atmosphereVolume.inScattering.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &inScatteringInfo;

    writes[3].dstSet = atmosphereVolumeSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &transmittanceInfo;

    writes[4].dstSet = atmosphereVolumeSet;
    writes[4].dstBinding = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &inScatteringInfo;

    device.updateDescriptorSets(writes);
}

void AtmosphericScattering2::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void AtmosphericScattering2::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void AtmosphericScattering2::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    preview.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("preview.vert.spv"))
                .fragmentShader(resource("preview.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0)
            .inputAssemblyState()
                .triangleStrip()
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
                    .cullNone()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(settings.msaaSamples)
                .depthStencilState()
                    .disableDepthWrite()
                    .disableDepthTest()
                    .compareOpLess()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addDescriptorSetLayout(atmosphere->uboDescriptorSetLayout)
                    .addDescriptorSetLayout(atmosphere->lutDescriptorSetLayout)
                    .addDescriptorSetLayout(uboSetLayout)
                    .addDescriptorSetLayout(atmosphereVolumeSetLayout)
                .renderPass(renderPass)
                .subpass(0)
                .name("preview")
                .pipelineCache(pipelineCache)
            .build(preview.layout);
    //    @formatter:on
}

void AtmosphericScattering2::createComputePipelines() {
    auto module = device.createShaderModule(resource("camera_volume.comp.spv"));
    auto stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});

    cameraVolume.layout = device.createPipelineLayout(
            {atmosphere->uboDescriptorSetLayout, atmosphere->lutDescriptorSetLayout, uboSetLayout, cameraVolumeSetLayout});

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = cameraVolume.layout.handle;
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("camera_volume_layout",
                                                   cameraVolume.layout.handle);

    cameraVolume.pipeline = device.createComputePipeline(computeCreateInfo);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("camera_volume",
                                            cameraVolume.pipeline.handle);
}


void AtmosphericScattering2::onSwapChainDispose() {
    dispose(preview.pipeline);
}

void AtmosphericScattering2::onSwapChainRecreation() {
    initCamera();
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *AtmosphericScattering2::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    static std::array<VkDescriptorSet, 4> sets;
    sets[0] = atmosphere->uboDescriptorSet;
    sets[1] = atmosphere->lutDescriptorSet;
    sets[2] = uboSet;
    sets[3] = atmosphereVolumeSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, preview.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, preview.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);

    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void AtmosphericScattering2::renderUI(VkCommandBuffer commandBuffer) {
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
        exposure = 10.f * glm::pow(1.1f, power);
    }

    ImGui::ColorEdit3("sphere albedo", &ubo->sphereAlbedo.x);
    ImGui::ColorEdit3("ground albedo", &ubo->groundAlbedo.x);
    ImGui::End();

    static auto atmosphereUI = atmosphereGenerator->ui();

    atmosphereUI();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}


void AtmosphericScattering2::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }

    auto cam = camera->cam();

    ubo->inverse_projection = glm::inverse(cam.proj);
    ubo->inverse_view = glm::inverse(cam.view);
    ubo->camera = camera->position();
    ubo->exposure = exposure;
    ubo->frame++;
    updateSunDirection();
    createCameraVolume();

//    static float updateLut = 0;
//    updateLut += time;
//        atmosphereGenerator->generateLUT();
}

void AtmosphericScattering2::updateSunDirection() {
    glm::vec3 p = glm::vec3(1, 0, 0);
    auto axis = glm::angleAxis(glm::radians(sun.zenith), glm::vec3{0, 0, 1});
    p = axis * p;

    axis = glm::angleAxis(glm::radians(sun.azimuth), glm::vec3{0, 1, 0});
    p = axis * p;
    ubo->sun_direction = glm::normalize(p);

}

void AtmosphericScattering2::checkAppInputs() {
    camera->processInput();
}

void AtmosphericScattering2::cleanup() {
    VulkanBaseApp::cleanup();
}

void AtmosphericScattering2::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{
        Settings settings;
        settings.depthTest = true;

        auto app = AtmosphericScattering2{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}