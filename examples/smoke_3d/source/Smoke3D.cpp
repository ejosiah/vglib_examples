#include "Smoke3D.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "Vertex.h"

#include <cstddef>


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
    createCollider();
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
    numCells = simData.numCells;

    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    simDataBuffer = device.createDeviceLocalBuffer(&simData, sizeof(simData), usage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("sim_data_buffer", simDataBuffer.buffer);
}

void Smoke3D::initSolver() {
    fixedUpdate.frequency(120);
    auto temperatureAndDensityData = initTemperatureAndDensityField();

    fluidSolver =
        eular::FluidSolver::Builder{ &device, &descriptorPool }
        .gridSize(glm::vec3(simData.resolution))
        .closedDomain()
        .vorticityConfinementScale(6)
        .useMacCormackAdvection()
        .addQuantity(temperatureAndDensity, "temperature_and_density", temperatureAndDensityData)
        .addExternalForce(buoyancyForce())
        .addExternalForce(periodicWindForce())
        .useGaussSeidelSolver()
        .dt(fixedUpdate.period())
    .build();

    initObstacleCollider();
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

eular::ExternalForce Smoke3D::periodicWindForce() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        auto center = (simData.domain.min + simData.domain.max) * 0.5f;
        const auto c = glm::cos(windControls.angle);
        const auto s = glm::sin(windControls.angle);
        const glm::vec3 radial{c, 0.0f, s};
        const glm::vec3 direction{-c, 0.0f, -s};
        const float height = glm::mix(simData.domain.min.y, simData.domain.max.y, windControls.height);
        const glm::vec3 position{center.x + windControls.distance * radial.x, height,
                                 center.z + windControls.distance * radial.z};

        windConstants.positionRadius = glm::vec4(position, windControls.radius);
        windConstants.directionStrength = glm::vec4(direction, windControls.strength);
        windConstants.time = fluidSolver->elapsedTime();
        windConstants.period = windControls.period;
        windConstants.enabled = windControls.enabled ? 1u : 0u;
        windConstants.pulseMin = windControls.pulseMin;

        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = forceFieldSets[eular::in];
        sets[1] = forceFieldSets[eular::out];
        sets[2] = simDescriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("periodic_wind_force"));
        vkCmdPushConstants(commandBuffer, compute.layout("periodic_wind_force"), VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(windConstants), &windConstants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("periodic_wind_force"),
                                0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
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

    // temperatureAndDensity.postAdvectActions.emplace_back([&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3 gc){
    //     return decaySmoke(commandBuffer, field, gc);
    // });

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

void Smoke3D::createCollider() {
    auto prim = primitives::sphere(10, 10, obstacle.radius, glm::translate(glm::mat4{1}, obstacle.position), {1, 1, 0, 1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    sphere.vertices = device.createDeviceLocalBuffer(prim.vertices.data(), BYTE_SIZE(prim.vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    sphere.indices = device.createDeviceLocalBuffer(prim.indices.data(), BYTE_SIZE(prim.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void Smoke3D::initObstacleCollider() {
    const auto width = static_cast<uint32_t>(simData.resolution.x);
    const auto height = static_cast<uint32_t>(simData.resolution.y);
    const auto depth = static_cast<uint32_t>(simData.resolution.z);
    const auto cellCount = width * height * depth;

    obstacleColliderField.name = "smoke_3d_obstacle_collider";
    obstacleColliderVelocityField.name = "smoke_3d_obstacle_collider_velocity";

    std::vector<glm::vec2> colliderData(cellCount, glm::vec2{1.0f, eular::colliderTypeValue(eular::ColliderType::Sdf)});
    std::vector<glm::vec2> velocityData(cellCount, glm::vec2{0.0f});

    for(uint32_t z = 0; z < depth; ++z) {
        for(uint32_t y = 0; y < height; ++y) {
            for(uint32_t x = 0; x < width; ++x) {
                const glm::vec3 uvw = (glm::vec3{x, y, z} + 0.5f) / glm::vec3{simData.resolution};
                const glm::vec3 position = glm::mix(simData.domain.min, simData.domain.max, uvw);
                const float sdf = glm::length(position - obstacle.position) - obstacle.radius;
                const auto index = (z * height + y) * width + x;
                colliderData[index] = {sdf, eular::colliderTypeValue(eular::ColliderType::Sdf)};
            }
        }
    }

    for(auto i = 0u; i < 2; ++i) {
        textures::create(device, obstacleColliderField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT,
                         colliderData.data(), {width, height, depth}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         sizeof(glm::vec2));
        textures::create(device, obstacleColliderVelocityField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT,
                         velocityData.data(), {width, height, depth}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         sizeof(glm::vec2));
        obstacleColliderField[i].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
        obstacleColliderVelocityField[i].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

        device.setName<VK_OBJECT_TYPE_IMAGE>(std::format("{}_{}", obstacleColliderField.name, i),
                                             obstacleColliderField[i].image.image);
        device.setName<VK_OBJECT_TYPE_IMAGE>(std::format("{}_{}", obstacleColliderVelocityField.name, i),
                                             obstacleColliderVelocityField[i].image.image);
    }

    auto writes = initializers::writeDescriptorSets<12>();
    auto writeOffset = createFieldDescriptorSet(writes, 0, obstacleColliderField);
    writeOffset = createFieldDescriptorSet(writes, writeOffset, obstacleColliderVelocityField);
    writes.resize(writeOffset);
    device.updateDescriptorSets(writes);

    for(auto& write : writes) {
        delete write.pImageInfo;
    }

    const std::array<eular::Collider, 1> colliders{{
        {obstacleColliderField.descriptorSet[eular::in], obstacleColliderVelocityField.descriptorSet[eular::in]}
    }};
    fluidSolver->setColliders(colliders);
}

uint32_t Smoke3D::createFieldDescriptorSet(std::vector<VkWriteDescriptorSet>& writes, uint32_t writeOffset, eular::Field& field) {
    auto sets = descriptorPool.allocate({fluidSolver->fieldDescriptorSetLayout(), fluidSolver->fieldDescriptorSetLayout()});

    field.descriptorSet[0] = sets[0];
    field.descriptorSet[1] = sets[1];

    for(uint32_t i = 0; i < 2; ++i) {
        writes[writeOffset].dstSet = field.descriptorSet[i];
        writes[writeOffset].dstBinding = 0;
        writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[writeOffset].descriptorCount = 1;
        writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[i].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        ++writeOffset;

        writes[writeOffset].dstSet = field.descriptorSet[i];
        writes[writeOffset].dstBinding = 1;
        writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        writes[writeOffset].descriptorCount = 1;
        writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[i].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        ++writeOffset;

        writes[writeOffset].dstSet = field.descriptorSet[i];
        writes[writeOffset].dstBinding = 2;
        writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[writeOffset].descriptorCount = 1;
        writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[i].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        ++writeOffset;
    }

    return writeOffset;
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

    auto atomicFeatures = findExtension<VkPhysicalDeviceShaderAtomicFloatFeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT, deviceCreateNextChain);
    atomicFeatures->shaderBufferFloat32AtomicAdd = VK_TRUE;
    atomicFeatures->shaderBufferFloat32Atomics = VK_TRUE;

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
            .createLayout();

    simDescriptorSet = descriptorPool.allocate({ simDescriptorSetLayout }).front();
}

void Smoke3D::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<1>();
    VkDescriptorBufferInfo simInfo{ simDataBuffer, 0, VK_WHOLE_SIZE };

    writes[0].dstSet = simDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &simInfo;

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
    //    @formatter:off
        vector.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("render_vector_field.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .name("render_vector_field")
                .layout()
                    .addDescriptorSetLayout(sourceFieldSetLayouts[0])
                    .addDescriptorSetLayout(sourceFieldSetLayouts[0])
                    .addDescriptorSetLayout(sourceFieldSetLayouts[0])
                    .addDescriptorSetLayout(sourceFieldSetLayouts[0])
                    .addDescriptorSetLayout(simDescriptorSetLayout)
                .build(vector.layout);

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

        renderVectorField(commandBuffer);
        renderObstacle(commandBuffer);
        renderSmoke(commandBuffer);

        if (showOutline) {
            renderEmitter(commandBuffer);
            renderDomain(commandBuffer);
        }
        renderUI(commandBuffer);
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

void Smoke3D::renderObstacle(VkCommandBuffer commandBuffer) {
    AppContext::renderSolid(commandBuffer, *camera, [&]() {
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, sphere.vertices, &offset);
        vkCmdBindIndexBuffer(commandBuffer, sphere.indices, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, sphere.indices.sizeAs<uint>(), 1, 0, 0, 0);
    });
}

void Smoke3D::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Smoke 3D");
    ImGui::SetWindowSize({});

    ImGui::Checkbox("Wind", &windControls.enabled);
    ImGui::SliderAngle("Rotate Y", &windControls.angle, 0.0f, 360.0f);
    ImGui::SliderFloat("Height", &windControls.height, 0.0f, 1.0f);
    ImGui::SliderFloat("Distance", &windControls.distance, 0.0f, 1.5f);
    ImGui::SliderFloat("Radius", &windControls.radius, 0.02f, 1.0f);
    ImGui::SliderFloat("Strength", &windControls.strength, 0.0f, 80.0f);
    ImGui::SliderFloat("Period", &windControls.period, 0.1f, 8.0f);
    ImGui::SliderFloat("Minimum pulse", &windControls.pulseMin, 0.0f, 1.0f);

    ImGui::Checkbox("Outline", &showOutline);

    ImGui::End();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void Smoke3D::renderVectorField(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 5> sets;
    sets[0] = fluidSolver->vectorField().u.descriptorSet[eular::in];
    sets[1] = fluidSolver->vectorField().v.descriptorSet[eular::in];
    sets[2] = fluidSolver->vectorField().w.descriptorSet[eular::in];
    sets[3] = temperatureAndDensity.field.descriptorSet[eular::in];
    sets[4] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vector.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vector.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, vector.layout);
    AppContext::drawVector(commandBuffer, numCells);
}

void Smoke3D::clearTemperatureSum(VkCommandBuffer commandBuffer) {
    vkCmdFillBuffer(commandBuffer, simDataBuffer, offsetof(SimData, tempSum), sizeof(float), 0);
    Barrier::transferWriteToComputeRead(commandBuffer, simDataBuffer);
}

void Smoke3D::emitSmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    clearTemperatureSum(commandBuffer);

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = field.descriptorSet[eular::in];
    sets[1] = field.descriptorSet[eular::out];
    sets[2] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("smoke_source"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("smoke_source"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    Barrier::computeWriteToRead(commandBuffer, simDataBuffer);
    field.swap();
}

bool Smoke3D::decaySmoke(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = field.descriptorSet[eular::in];
    sets[1] = field.descriptorSet[eular::out];
    sets[2] = simDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("decay_smoke"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("decay_smoke"),
                            0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
    return true;
}

void Smoke3D::updateAmbientTemperature(VkCommandBuffer commandBuffer, eular::Field &field, glm::uvec3 gc) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("update_ambient_temperature"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("update_ambient_temperature"),
                            0, 1, &simDescriptorSet, 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, 1, 1, 1);
    Barrier::computeWriteToRead(commandBuffer, simDataBuffer);
}

void Smoke3D::update(float time) {
    auto title = fmt::format("{}, camera: {}, fps {}", this->title, camera->position(), framePerSecond);
    glfwSetWindowTitle(window, title.c_str());
    fixedUpdate.advance(time);

    if (!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
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
            .name = "decay_smoke",
            .shadePath = resource("decay_smoke.comp.spv"),
            .layouts = { &sourceFieldSetLayouts[0], &sourceFieldSetLayouts[1], &simDescriptorSetLayout }
        },
        {
            .name = "buoyancy_force",
            .shadePath = resource("buoyancy_force.comp.spv"),
            .layouts = { &forceFieldSetLayouts[0], &forceFieldSetLayouts[1], &sourceFieldSetLayouts[0]
                        , &simDescriptorSetLayout }
        },
        {
            .name = "periodic_wind_force",
            .shadePath = resource("periodic_wind_force.comp.spv"),
            .layouts = { &forceFieldSetLayouts[0], &forceFieldSetLayouts[1], &simDescriptorSetLayout },
            .ranges = {{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(WindConstants) }}
        },
        {
            .name = "update_ambient_temperature",
            .shadePath = resource("update_ambient_temperature.comp.spv"),
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
        settings.deviceExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
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
