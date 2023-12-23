#include "AtmosphericScattering.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"

AtmosphericScattering::AtmosphericScattering(const Settings& settings) : VulkanBaseApp("Precomputed Atmospheric Scattering", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering/data");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering/spv");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering/models");
    fileManager.addSearchPathFront("../../examples/atmospheric_scattering/textures");
}

void AtmosphericScattering::initApp() {
    loadAtmosphereLUT();
    initUbo();
    initBuffers();
    initCamera();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void AtmosphericScattering::loadAtmosphereLUT() {
    auto data = loadFile(resource("irradiance.dat"));

    textures::create(device ,atmosphereLUT.irradiance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );


    data = loadFile(resource("transmittance.dat"));
    textures::create(device ,atmosphereLUT.transmittance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );

    data = loadFile(resource("scattering.dat"));
    textures::create(device ,atmosphereLUT.scattering, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, data.data(),
                     {SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float) );
}

void AtmosphericScattering::initBuffers() {
    auto quad = ClipSpace::Quad::positions;
    std::vector<glm::vec4> positions;
    for(int i = 0; i < quad.size(); i+= 2){
        glm::vec4 p{quad[i], 0, 1};
        positions.push_back(p);
    }
    
    screenBuffer = device.createDeviceLocalBuffer(positions.data(), BYTE_SIZE(positions), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void AtmosphericScattering:: initUbo() {
    uboBuffer = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(Ubo), "uniforms");
    ubo = reinterpret_cast<Ubo*>(uboBuffer.map());

    ubo->white_point = glm::vec3(1);
    ubo->earth_center = {0, 0, -6360000 / kLengthUnitInMeters};
    ubo->sun_size = glm::vec3(glm::tan(kSunAngularRadius), glm::cos(kSunAngularRadius), 0);
    ubo->sphereAlbedo = glm::vec3(0.8);
    ubo->groundAlbedo = {0.0, 0.0, 0.04};
}

void AtmosphericScattering::initCamera() {
}


void AtmosphericScattering::createDescriptorPool() {
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

void AtmosphericScattering::createDescriptorSetLayouts() {
    atmosphereLutSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("atmosphere")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
    uboSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ubo")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void AtmosphericScattering::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ atmosphereLutSetLayout, uboSetLayout });
    atmosphereLutSet = sets[0];
    uboSet = sets[1];

    auto writes = initializers::writeDescriptorSets<5>();

    writes[0].dstSet = atmosphereLutSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{atmosphereLUT.irradiance.sampler.handle, atmosphereLUT.irradiance.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &irradianceInfo;

    writes[1].dstSet = atmosphereLutSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo transmittanceInfo{atmosphereLUT.transmittance.sampler.handle, atmosphereLUT.transmittance.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &transmittanceInfo;

    writes[2].dstSet = atmosphereLutSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo scatteringInfo{atmosphereLUT.scattering.sampler.handle, atmosphereLUT.scattering.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &scatteringInfo;

    // single_mie_scattering
    writes[3] = writes[2];
    writes[3].dstBinding = 3;

    writes[4].dstSet = uboSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ uboBuffer, 0, VK_WHOLE_SIZE };
    writes[4].pBufferInfo = &uboInfo;



    device.updateDescriptorSets(writes);
}

void AtmosphericScattering::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void AtmosphericScattering::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void AtmosphericScattering::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    render.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("vertex.vert.spv"))
                .fragmentShader(resource("fragment.frag.spv"))
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
                .layout()
                    .addDescriptorSetLayout(atmosphereLutSetLayout)
                    .addDescriptorSetLayout(uboSetLayout)
                .renderPass(renderPass)
                .subpass(0)
                .name("render")
                .pipelineCache(pipelineCache)
            .build(render.layout);
    //    @formatter:on
}


void AtmosphericScattering::onSwapChainDispose() {
    dispose(render.pipeline);
}

void AtmosphericScattering::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *AtmosphericScattering::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    renderAtmosphere(commandBuffer);
    renderUI(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void AtmosphericScattering::renderAtmosphere(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = atmosphereLutSet;
    sets[1] = uboSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenBuffer, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void AtmosphericScattering::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Atmospheric scattering");
    ImGui::SetWindowSize({0, 0});

    static float exposureScale = 0.5;
    if(ImGui::SliderFloat("exposure", &exposureScale, 0, 1)){
        float power = remap(exposureScale, 0, 1, -20, 20);
        exposure = 10.f * glm::pow(1.1f, power);
    }

    ImGui::ColorEdit3("sphere albedo", &ubo->sphereAlbedo.x);
    ImGui::ColorEdit3("ground albedo", &ubo->groundAlbedo.x);

    if(ImGui::Button("Cycle views")){
        view++;
        view %= numViews;

        switch(view){
            case 0:
                setView(9000, 1.47, 0, 1.3, 3, 10);
                break;
            case 1:
                setView(9000, 1.47, 0, 1.564, -3, 10);
                break;
            case 2:
                setView(7000, 1.57, 0, 1.54, -2.96, 10);
                break;
            case 3:
                setView(7000, 1.57, 0, 1.328, -3.044, 10);
                break;
            case 4:
                setView(9000, 1.39, 0, 1.2, 0.7, 10);
                break;
            case 5:
                setView(9000, 1.5, 0, 1.628, 1.05, 200);
                break;
            case 6:
                setView(7000, 1.43, 0, 1.57, 1.34, 40);
                break;
            case 7:
                setView(2.7e6, 0.81, 0, 1.57, 2, 10);
                break;
            case 8:
                setView(1.2e7, 0.0, 0, 0.93, -2, 10);
                break;
        }
    }
    ImGui::Text("left mouse to move view, left mouse + ctrl to move sun");
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void AtmosphericScattering::update(float time) {
    const auto kFovY = 50.f / 180.f * glm::pi<float>();
    const auto kTanFovY = glm::tan(kFovY / 2);
    const auto aspectRatio = swapChain.aspectRatio();

    ubo->view_from_clip = glm::transpose(glm::mat4{
            kTanFovY * aspectRatio, 0, 0, 0,
            0, kTanFovY, 0, 0,
            0, 0, 0, -1,
            0, 0, 1, 1
    });

    const auto cosZ = glm::cos(viewZenithAngleRadians);
    const auto sinZ = glm::sin(viewZenithAngleRadians);
    const auto cosA = glm::cos(viewAzimuthAngleRadians);
    const auto sinA = glm::sin(viewAzimuthAngleRadians);
    const auto viewDistance = viewDistanceMeters / kLengthUnitInMeters;

    ubo->model_from_view = glm::transpose(glm::mat4{
            -sinA, -cosZ * cosA,  sinZ * cosA, sinZ * cosA * viewDistance,
            cosA, -cosZ * sinA, sinZ * sinA, sinZ * sinA * viewDistance,
            0, sinZ, cosZ, cosZ * viewDistance,
            0, 0, 0, 1
    });

    ubo->exposure = exposure;
    ubo->sun_direction = glm::vec3{glm::cos(sunAzimuthAngleRadians) *
                                   glm::sin(sunZenithAngleRadians),
                                   glm::sin(sunAzimuthAngleRadians) *
                                   glm::sin(sunZenithAngleRadians),
                                   glm::cos(sunZenithAngleRadians)};

    ubo->camera = glm::column(ubo->model_from_view, 3);
}

void AtmosphericScattering::checkAppInputs() {
    constexpr float scale = 500;
    const auto dx = mouse.relativePosition.x;
    const auto dy = mouse.relativePosition.y;

    if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        if(ImGui::GetIO().KeyCtrl) { // move sun
            sunZenithAngleRadians -= dy / scale;
            sunZenithAngleRadians = glm::clamp(sunZenithAngleRadians, 0.f, glm::pi<float>());
            sunAzimuthAngleRadians += dx/scale;
        }else { // move view
            viewZenithAngleRadians += dy / scale;
            viewZenithAngleRadians = glm::clamp(viewZenithAngleRadians, 0.f, glm::half_pi<float>());
            viewAzimuthAngleRadians += dx / scale;
        }
    }

    if(ImGui::GetIO().MouseWheel != 0){
        viewDistanceMeters *= ImGui::GetIO().MouseWheel > 0 ? 1.05 : 1 / 1.05;
    }
}

void AtmosphericScattering::cleanup() {
}

void AtmosphericScattering::onPause() {
}

void
AtmosphericScattering::setView(float viewDistanceMeters, float viewZenithAngleRadians, float viewAzimuthAngleRadians,
                               float sunZenithAngleRadians, float sunAzimuthAngleRadians, float exposure) {

    this->viewDistanceMeters = viewDistanceMeters;
    this->viewZenithAngleRadians = viewZenithAngleRadians;
    this->viewAzimuthAngleRadians = viewAzimuthAngleRadians;
    this->sunZenithAngleRadians = sunZenithAngleRadians;
    this->sunAzimuthAngleRadians = sunAzimuthAngleRadians;
    this->exposure = exposure;

}


int main(){
    try{

        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;

        auto app = AtmosphericScattering{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}