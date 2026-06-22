#include "Smoke3D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "Vertex.h"


Smoke3D::Smoke3D(const Settings& settings) : VulkanBaseApp("smoke 3d", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("smoke_3d");
    fileManager().addSearchPathFront("smoke_3d/data");
    fileManager().addSearchPathFront("smoke_3d/spv");
    fileManager().addSearchPathFront("smoke_3d/models");
    fileManager().addSearchPathFront("smoke_3d/textures");
}

void Smoke3D::initApp() {
    initSimData();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    initSolver();
    createCommandPool();
    createPipelineCache();
    createComputePipeline();
    createRenderPipeline();
}

void Smoke3D::initSimData() {
    simData = {};
    simData.worldToVoxel = toLocalSpace(simData.domain);
    simData.voxelToWorld = glm::inverse(simData.worldToVoxel);
    simData.numCells = simData.resolution.x * simData.resolution.y * simData.resolution.z;
    ambientTemperatureGroupCount = {
        (static_cast<uint32_t>(simData.resolution.x) + 7u) / 8u,
        (static_cast<uint32_t>(simData.resolution.z) + 7u) / 8u
    };

    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    simDataBuffer = device.createDeviceLocalBuffer(&simData, sizeof(simData), usage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("sim_data_buffer", simDataBuffer.buffer);

    const auto numAmbientTemperatureGroups = ambientTemperatureGroupCount.x * ambientTemperatureGroupCount.y;
    ambientTemperaturePartialSums = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
                                                        sizeof(float) * numAmbientTemperatureGroups,
                                                        "ambient_temperature_partial_sums");
}

void Smoke3D::initSolver() {
    fixedUpdate.frequency(120);
    auto temperatureAndDensityData = initTemperatureAndDensityField();

    fluidSolver =
        eular::FluidSolver::Builder{ &device, &descriptorPool }
        .gridSize(glm::vec3(simData.resolution))
        .closedDomain()
        .vorticityConfinementScale(6)
        .addQuantity(temperatureAndDensity, "temperature_and_density", temperatureAndDensityData)
        .addExternalForce(buoyancyForce())
        .useGaussSeidelSolver()
        // .poissonIterations(100)
        .dt(fixedUpdate.period())
    .build();
}

eular::ExternalForce Smoke3D::buoyancyForce() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        static std::array<VkDescriptorSet, 4> sets;
        sets[0] = forceFieldSets[eular::in];
        sets[1] = forceFieldSets[eular::out];
        sets[2] = temperatureAndDensity.field.descriptorSet[eular::in];
        sets[3] = simDescriptorSet;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("buoyancy_force"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("buoyancy_force"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    };
}

std::vector<glm::vec4> Smoke3D::initTemperatureAndDensityField() {
    std::vector<glm::vec4> field(simData.numCells, glm::vec4(0));

    //    temperatureAndDensity.diffuseRate = 1e-7;
    temperatureAndDensity.diffuseRate = 0;
    temperatureAndDensity.update = [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
        emitSmoke(commandBuffer, field, gc);
    };

    temperatureAndDensity.postAdvectActions.emplace_back([&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
        return decaySmoke(commandBuffer, field, gc);
    });

    temperatureAndDensity.postAdvectActions.emplace_back(
        [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
            updateAmbientTemperature(commandBuffer, field, gc);
            return false;
        }
    );

    return field;
}

void Smoke3D::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.velocity = glm::vec3{5};
    cameraSettings.acceleration = glm::vec3(5);
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.horizontalFov = true;
    camera = std::make_unique<SpectatorCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({-5, 2, 3}, {0, 0, 0}, {0, 1, 0});
}

void Smoke3D::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void Smoke3D::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void Smoke3D::createDescriptorPool() {
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


void Smoke3D::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void Smoke3D::createDescriptorSetLayouts() {
    simDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("smoke_3d_sim_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    simDescriptorSet = descriptorPool.allocate({ simDescriptorSetLayout }).front();
}

void Smoke3D::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<2>();
    VkDescriptorBufferInfo simInfo{ simDataBuffer, 0, VK_WHOLE_SIZE };
    VkDescriptorBufferInfo ambientTemperatureInfo{ ambientTemperaturePartialSums, 0, VK_WHOLE_SIZE };

    writes[0].dstSet = simDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &simInfo;

    writes[1].dstSet = simDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &ambientTemperatureInfo;

    device.updateDescriptorSets(writes);
}

void Smoke3D::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Smoke3D::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void Smoke3D::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .dynamicState()
                    .primitiveTopology()
                .name("render")
                .build(render.layout);

        auto rayMarchBuilder = prototypes->cloneGraphicsPipeline();
        rayMarch.pipeline =
            rayMarchBuilder
                .shaderStage()
                    .vertexShader(resource("ray_march.vert.spv"))
                    .fragmentShader(resource("smoke_ray_march.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescriptions(ClipSpace::bindingDescription())
                    .addVertexAttributeDescriptions(ClipSpace::attributeDescriptions())
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .disableDepthTest()
                    .disableDepthWrite()
                    .compareOpAlways()
                .colorBlendState()
                    .attachment()
                        .clear()
                        .enableBlend()
                        .srcColorBlendFactor().one()
                        .dstColorBlendFactor().srcAlpha()
                        .srcAlphaBlendFactor().zero()
                        .dstAlphaBlendFactor().one()
                        .add()
                .layout().clear()
                    .addPushConstantRange(Camera::pushConstant(VK_SHADER_STAGE_ALL_GRAPHICS))
                    .addDescriptorSetLayout(sourceFieldSetLayouts[0])
                    .addDescriptorSetLayout(simDescriptorSetLayout)
                .name("smoke_ray_march")
                .build(rayMarch.layout);
    //    @formatter:on
}

void Smoke3D::createComputePipeline() {
    sourceFieldSetLayouts = fluidSolver->sourceFieldSetLayouts();
    forceFieldSetLayouts = fluidSolver->forceFieldSetLayouts();
    compute = ComputePipelines{&device, pipelines()};
    compute.createPipelines();
}


void Smoke3D::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(rayMarch.pipeline);
}

void Smoke3D::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *Smoke3D::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    fixedUpdate([&] {
        fluidSolver->runSimulation(commandBuffer);
    });

    Barrier::computeWriteToFragmentRead(commandBuffer);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        AppContext::renderFloor(commandBuffer, *camera);

        renderSmoke(commandBuffer);
        // renderEmitter(commandBuffer);
        // renderDomain(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Smoke3D::renderDomain(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout, simData.voxelToWorld * unitCubeToVoxel);
    AppContext::drawCubeOutline(commandBuffer);
}

void Smoke3D::renderEmitter(VkCommandBuffer commandBuffer) {
    glm::mat4 transform = glm::inverse(toLocalSpace(simData.emitterBounds)) * unitCubeToVoxel;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    camera->push(commandBuffer, render.layout, transform);
    AppContext::drawCubeOutline(commandBuffer);
}

void Smoke3D::renderSmoke(VkCommandBuffer commandBuffer) {

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = temperatureAndDensity.field.descriptorSet[eular::in];
    sets[1] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, rayMarch.layout, VK_SHADER_STAGE_ALL_GRAPHICS);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void Smoke3D::emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = field.descriptorSet[eular::in];
    sets[1] = field.descriptorSet[eular::out];
    sets[2] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("smoke_source"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("smoke_source"),
                            0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    field.swap();
}

bool Smoke3D::decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    return false;
}

void Smoke3D::updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = field.descriptorSet[eular::in];
    sets[1] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("update_ambient_temperature"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("update_ambient_temperature"),
                            0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, ambientTemperatureGroupCount.x, 1, ambientTemperatureGroupCount.y);
    Barrier::computeWriteToRead(commandBuffer, ambientTemperaturePartialSums);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("finalize_ambient_temperature"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("finalize_ambient_temperature"),
                            0, 1, &simDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToRead(commandBuffer, simDataBuffer);
}

void Smoke3D::update(float time) {
    auto title = fmt::format("{}, fps {}", this->title, framePerSecond);
    glfwSetWindowTitle(window, title.c_str());
    fixedUpdate.advance(time);
    camera->update(time);
    auto cam = camera->cam();
}

void Smoke3D::checkAppInputs() {
    camera->processInput();
}

void Smoke3D::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void Smoke3D::onPause() {
    VulkanBaseApp::onPause();
}

std::vector<PipelineMetaData> Smoke3D::pipelines() {
    assert(sourceFieldSetLayouts.size() == 2);

    return {
        {
            .name = "smoke_source",
            .shadePath = resource("smoke_source.comp.spv"),
            .layouts = { &sourceFieldSetLayouts[0], &sourceFieldSetLayouts[1], &simDescriptorSetLayout }
        },
        {
            .name = "buoyancy_force",
            .shadePath = resource("buoyancy_force.comp.spv"),
            .layouts = { &forceFieldSetLayouts[0], &forceFieldSetLayouts[1], &sourceFieldSetLayouts[0]
                        , &simDescriptorSetLayout }
        },
        {
            .name = "update_ambient_temperature",
            .shadePath = resource("update_ambient_temperature.comp.spv"),
            .layouts = { &sourceFieldSetLayouts[0], &simDescriptorSetLayout }
        },
        {
            .name = "finalize_ambient_temperature",
            .shadePath = resource("finalize_ambient_temperature.comp.spv"),
            .layouts = { &simDescriptorSetLayout }
        }
    };
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
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = Smoke3D{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
