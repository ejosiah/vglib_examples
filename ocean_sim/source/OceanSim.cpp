#include "OceanSim.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"

OceanSim::OceanSim(const Settings& settings) : VulkanBaseApp("Ocean simulation", settings) {
    fileManager.addSearchPathFront(".");
    fileManager.addSearchPathFront("../../examples/ocean_sim");
    fileManager.addSearchPathFront("../../examples/ocean_sim/data");
    fileManager.addSearchPathFront("../../examples/ocean_sim/spv");
    fileManager.addSearchPathFront("../../examples/ocean_sim/models");
    fileManager.addSearchPathFront("../../examples/ocean_sim/resources");
}

void OceanSim::initApp() {
    initCamera();
    loadEnvironmentMap();
    initBuffers();
    loadQuadPatch();
    initTerrain();
    initOcean();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
}

void OceanSim::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.aspectRatio = float(width)/float(height);
    cameraSettings.zNear = 50 * meter;
    cameraSettings.zFar = 60 * km;
    cameraSettings.acceleration = glm::vec3(1 * km);
    cameraSettings.velocity = glm::vec3(2 * km);
    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt(glm::vec3(0 * km, 1.2 * km, 2.5 * km), glm::vec3(0, 0, 0), {0, 1, 0});
}

void OceanSim::loadEnvironmentMap() {
    textures::fromFile(device, environmentTextures.brdfLUT, resource("brdf_lut.png"), true);
    textures::fromFile(device, environmentTextures.environmentMap, resource("sky.png"), true);
    textures::fromFile(device, environmentTextures.diffuseEnvironmentMap, resource("sky_diffuse.png"), true);

    textures::fromFile(device, heightMap, resource("kauai.png"));
}

void OceanSim::initBuffers() {
    const auto cubePrimitive = primitives::cube();
    std::vector<glm::vec3> cube;
    for(const auto& vertex : cubePrimitive.vertices){
        cube.push_back(vertex.position.xyz());
    }
    skyBox.vertexBuffer = device.createDeviceLocalBuffer(cube.data(), BYTE_SIZE(cube), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skyBox.indexBuffer = device.createDeviceLocalBuffer(cubePrimitive.indices.data(), BYTE_SIZE(cubePrimitive.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void OceanSim::loadQuadPatch() {
    auto patches = loadFile(resource("quad_patch.dat"));
    patch.numVertices = patches.size()/sizeof(glm::vec3);
    patch.numPatches = patch.numVertices/4;
    patch.vertexBuffer = device.createDeviceLocalBuffer(patches.data(), patches.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    spdlog::info("quad patched loaded with {} patches and {} vertices", patch.numPatches, patch.numVertices);
}

void OceanSim::initTerrain() {
    glm::mat4 xform = glm::translate(glm::mat4(1), {0, terrain.zMin, 0});
    terrain.transform = glm::scale(xform, {terrain.width, terrain.zMax, terrain.height});
}

void OceanSim::initOcean() {
    ocean.transform = glm::scale(glm::mat4(1), {ocean.width, 1, ocean.height});
}


void OceanSim::createDescriptorPool() {
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

void OceanSim::createDescriptorSetLayouts() {
    environmentSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("environment_map")
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
        .createLayout();

    heightMapSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("terrain_height_map")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)
        .createLayout();
    
    auto sets = descriptorPool.allocate({ environmentSetLayout, heightMapSetLayout });
    environmentSet = sets[0];
    heightMapSet = sets[1];
}

void OceanSim::updateDescriptorSets(){
    
    auto writes = initializers::writeDescriptorSets<4>();
    
    writes[0].dstSet = environmentSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo envInfo{ environmentTextures.environmentMap.sampler, environmentTextures.environmentMap.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &envInfo;

    writes[1].dstSet = environmentSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo envDiffuseInfo{ environmentTextures.diffuseEnvironmentMap.sampler, environmentTextures.diffuseEnvironmentMap.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &envDiffuseInfo;

    writes[2].dstSet = environmentSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo brdfLUTInfo{ environmentTextures.brdfLUT.sampler, environmentTextures.brdfLUT.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[2].pImageInfo = &brdfLUTInfo;

    writes[3].dstSet = heightMapSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo heightMapInfo{ heightMap.sampler, heightMap.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &heightMapInfo;

    device.updateDescriptorSets(writes);
}

void OceanSim::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void OceanSim::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void OceanSim::createRenderPipeline() {
    //    @formatter:off
    auto builder = device.graphicsPipelineBuilder();
    sky.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("sky.vert.spv"))
                .fragmentShader(resource("sky.frag.spv"))
            .vertexInputState()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
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
                    .cullFrontFace()
                    .frontFaceCounterClockwise()
                    .polygonModeFill()
                .multisampleState()
                    .rasterizationSamples(settings.msaaSamples)
                .depthStencilState()
                    .enableDepthWrite()
                    .enableDepthTest()
                    .compareOpLessOrEqual()
                    .minDepthBounds(0)
                    .maxDepthBounds(1)
                .colorBlendState()
                    .attachment()
                    .add()
                .layout()
                    .addPushConstantRange(Camera::pushConstant())
                    .addDescriptorSetLayout(environmentSetLayout)
                .renderPass(renderPass)
                .subpass(0)
                .name("sky")
                .pipelineCache(pipelineCache)
            .build(sky.layout);

    terrain.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .tessellationControlShader(resource("terrain.tesc.spv"))
                .tessellationEvaluationShader(resource("terrain.tese.spv"))
                .fragmentShader(resource("terrain.frag.spv"))
            .inputAssemblyState()
                .patches()
            .tessellationState()
                .patchControlPoints(4)
                .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
            .rasterizationState()
                .cullBackFace()
//                .polygonModeLine()
            .depthStencilState()
                .compareOpLess()
            .layout().clear()
                .addDescriptorSetLayout(heightMapSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 0, sizeof(Camera))
            .name("terrain")
            .pipelineCache(pipelineCache)
        .build(terrain.layout);


    ocean.pipeline =
        builder
            .shaderStage()
                .tessellationControlShader(resource("ocean.tesc.spv"))
                .tessellationEvaluationShader(resource("ocean.tese.spv"))
                .fragmentShader(resource("ocean.frag.spv"))
            .depthStencilState()
                .compareOpLessOrEqual()
            .name("ocean")
        .build(ocean.layout);
    //    @formatter:on
}

void OceanSim::createComputePipeline() {
    auto module = VulkanShaderModule{ "../../data/shaders/pass_through.comp.spv", device};
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache);
}


void OceanSim::onSwapChainDispose() {
    dispose(sky.pipeline);
    dispose(compute.pipeline);
}

void OceanSim::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *OceanSim::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    renderSkyBox(commandBuffer);
    renderTerrain(commandBuffer);
//    renderOcean(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void OceanSim::renderSkyBox(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sky.pipeline);
    camera->push(commandBuffer, sky.layout);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, sky.layout, 0, 1, &environmentSet, 0, nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skyBox.vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skyBox.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    auto size = skyBox.indexBuffer.sizeAs<uint32_t>();
    vkCmdDrawIndexed(commandBuffer, size, 1, 0, 0, 0);
}

void OceanSim::renderTerrain(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.pipeline);
    camera->push(commandBuffer, terrain.layout, terrain.transform, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, terrain.layout, 0, 1, &heightMapSet, 0, nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, patch.vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, patch.numVertices, 1, 0, 0);
}


void OceanSim::renderOcean(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ocean.pipeline);
    camera->push(commandBuffer, ocean.layout, ocean.transform, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ocean.layout, 0, 1, &heightMapSet, 0, nullptr);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, patch.vertexBuffer, &offset);
    vkCmdDraw(commandBuffer, patch.numVertices, 1, 0, 0);
}

void OceanSim::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void OceanSim::checkAppInputs() {
    camera->processInput();
}

void OceanSim::cleanup() {
    VulkanBaseApp::cleanup();
}

void OceanSim::onPause() {
    VulkanBaseApp::onPause();
}


int main(){
    try{

        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.enabledFeatures.tessellationShader = VK_TRUE;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

        auto app = OceanSim{ settings };
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}