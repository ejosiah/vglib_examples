#include "YetAnotherFluidSim.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"

#include <array>
#include <cmath>
#include <format>

YetAnotherFluidSim::YetAnotherFluidSim(const Settings& settings) : VulkanBaseApp("Yet another fluid simulation", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("yet_another_fluid_sim");
    fileManager().addSearchPathFront("yet_another_fluid_sim/data");
    fileManager().addSearchPathFront("yet_another_fluid_sim/spv");
    fileManager().addSearchPathFront("yet_another_fluid_sim/models");
    fileManager().addSearchPathFront("yet_another_fluid_sim/textures");
}

void YetAnotherFluidSim::initApp() {
    initCamera();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    initSceneProperties();
    initSolver();
    createComputePipeline();
    createRenderPipeline();
}

void YetAnotherFluidSim::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = swapChain.aspectRatio();

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void YetAnotherFluidSim::initSceneProperties() {
    auto cScale = to<float>(height)/simSize.y;
    simSize.x = to<float>(width)/ cScale;
    domainSize.x = domainSize.y / simSize.y * simSize.x;

    scenes[Scene::Tank] = {
        .color = {},
        .resolution = 50,
        .iterations = 40,
        .timeStep = 0.01666667,
        .gravity = -9.81,
        .pressure = true
    };

    scenes[Scene::WindTunnel] = {
        .resolution = 100,
        .iterations = 40,
        .timeStep = 1.f/120.f,
        .gravity =  0.0f,
        .smoke = true
    };

    scenes[Scene::Paint] = {
        .resolution = 100,
        .gravity =  0.0f,
        .smoke = true
    };

}

void YetAnotherFluidSim::initSolver() {
    visualizer.releaseDescriptorSets();
    releaseObstacleColliderDescriptorSets();
    solver.reset();

    const auto& props = scenes.at(scene);
    const auto h = domainSize.y/props.resolution;
    solverGridSize = { domainSize.x/h, domainSize.y/h };
    const auto simulationFrequency = to<int>(std::round(1.0f / props.timeStep));
    fixedUpdate.frequency(simulationFrequency > 0 ? simulationFrequency : 1);

    forceConstants.speed = props.gravity;
    forceConstants.mode = 0;

    auto builder =
        eular::FluidSolver::Builder{&device, &descriptorPool}
            .gridSize(solverGridSize)
            .density(1000.0f)
            .dt(props.timeStep)
            .poissonIterations(to<int>(props.iterations/2))
            .addExternalForce(force())
            .closedDomain()
            .useConjugateGradientSolver();

    if (scene == Scene::Tank) {
        builder.addExternalForce(force());
    }

    if (scene == Scene::WindTunnel || scene == Scene::Paint) {
        initSmoke();
        builder.addQuantity(smoke, "smoke", smokeField);
    }

    if (scene == Scene::WindTunnel) {
        forceConstants.speed = 2.f;
        forceConstants.mode = 1;

        // builder.vorticityConfinementScale(6.0f);
        builder.useMacCormackAdvection();
        builder.openBoundaryEdges(eular::FluidSolver::BoundaryEdgeRight);
    }
    if (scene == Scene::Paint) {
        forceConstants.mode = 2;
    }

    solver = builder.build();

    initObstacleCollider();
    initCpuSolver();
    initVisualizer();

    glm::mat4 projection = vkn::ortho(0, domainSize.x, 0, domainSize.y);
    glm::mat4 view = glm::scale(glm::mat4{1}, {1, 1, 1});
    obstacleConstants.transform = view * projection;
    obstacleConstants.size = 2.0f * obstacleConstants.radius * to<float>(height) / domainSize.y;
    obstacleConstants.color = props.color;
    obstacleConstants.domainMin = glm::vec2{0.0f};
    obstacleConstants.domainMax = domainSize;
    obstacleConstants.scene = static_cast<uint32_t>(scene);
    obstacleSimulationPosition = obstacleConstants.position;
    showPressure = props.pressure;
    showSmoke = props.smoke;
    showStreamLines = props.streamLines;
}

void YetAnotherFluidSim::initCpuSolver() {
    if (scene != Scene::Tank) {
        cpuSolver.reset();
        return;
    }

    const auto& props = scenes.at(scene);
    const auto sampleCount = to<std::size_t>(solverGridSize.x) * to<std::size_t>(solverGridSize.y);
    const auto fieldByteSize = to<VkDeviceSize>(sampleCount * sizeof(float));

    auto params = CpuFluidSolver::Params{
        .numX = to<int32_t>(solverGridSize.x),
        .numY = to<int32_t>(solverGridSize.y),
        .numIterations = to<int32_t>(props.iterations),
        .density = 1000.0f,
        .gravity = -std::abs(props.gravity),
        .h = 1.0f / to<float>(props.resolution),
        .dt = props.timeStep
    };

    cpuSolver = std::make_unique<CpuFluidSolver>(params);
    auto& mask = cpuSolver->fluidMask();
    const auto paddedSize = cpuSolver->size();
    for (auto x = 0; x < paddedSize.x; ++x) {
        for (auto y = 0; y < paddedSize.y; ++y) {
            const auto isSolid = x == 0 || x == paddedSize.x - 1 || y == 0;
            mask[cpuSolver->index(x, y)] = isSolid ? 0.0f : 1.0f;
        }
    }

    cpuSolver->updateObstacle({0.4, 0.5}, true);

    cpuUUpload.assign(sampleCount, 0.0f);
    cpuVUpload.assign(sampleCount, 0.0f);
    cpuPressureUpload.assign(sampleCount, 0.0f);
    cpuUUploadBuffer = device.createStagingBuffer(fieldByteSize);
    cpuVUploadBuffer = device.createStagingBuffer(fieldByteSize);
    cpuPressureUploadBuffer = device.createStagingBuffer(fieldByteSize);

    copyCpuFieldsToUploadBuffers();
}

void YetAnotherFluidSim::updateCpuSolver() {
    if (!cpuSolver) {
        return;
    }

    cpuSolver->simulate();
    copyCpuFieldsToUploadBuffers();
}

void YetAnotherFluidSim::copyCpuFieldsToUploadBuffers() {
    if (!cpuSolver) {
        return;
    }

    const auto& u = cpuSolver->uField();
    const auto& v = cpuSolver->vField();
    const auto& pressure = cpuSolver->pressureField();
    for (auto y = 0u; y < solverGridSize.y; ++y) {
        for (auto x = 0u; x < solverGridSize.x; ++x) {
            const auto dst = to<std::size_t>(y) * solverGridSize.x + x;
            const auto i = to<int32_t>(x + 1);
            const auto j = to<int32_t>(y + 1);
            const auto src = cpuSolver->index(i, j);
            cpuUUpload[dst] = 0.5f * (u[src] + u[cpuSolver->index(i + 1, j)]);
            cpuVUpload[dst] = 0.5f * (v[src] + v[cpuSolver->index(i, j + 1)]);
            cpuPressureUpload[dst] = pressure[src];
        }
    }

    const auto fieldByteSize = to<VkDeviceSize>(cpuUUpload.size() * sizeof(float));
    cpuUUploadBuffer.copy(cpuUUpload.data(), fieldByteSize);
    cpuVUploadBuffer.copy(cpuVUpload.data(), fieldByteSize);
    cpuPressureUploadBuffer.copy(cpuPressureUpload.data(), fieldByteSize);
}

void YetAnotherFluidSim::uploadCpuFields(VkCommandBuffer commandBuffer) {
    if (!cpuSolver) {
        return;
    }

    auto& vectorField = solver->vectorField();
    uploadCpuField(commandBuffer, cpuUUploadBuffer, vectorField.u);
    uploadCpuField(commandBuffer, cpuVUploadBuffer, vectorField.v);
    uploadCpuField(commandBuffer, cpuPressureUploadBuffer, solver->pressureField());
}

void YetAnotherFluidSim::uploadCpuField(VkCommandBuffer commandBuffer, const VulkanBuffer& buffer, eular::Field& field) {
    field[0].image.copyFromBuffer(commandBuffer, buffer, VK_IMAGE_LAYOUT_GENERAL);
    field[1].image.copyFromBuffer(commandBuffer, buffer, VK_IMAGE_LAYOUT_GENERAL);
    Barrier::transferWriteToComputeRead(commandBuffer);
}

void YetAnotherFluidSim::initObstacleCollider() {
    releaseObstacleColliderDescriptorSets();

    const auto width = solverGridSize.x;
    const auto height = solverGridSize.y;

    obstacleColliderField.name = "yet_another_fluid_sim_obstacle_collider";
    obstacleColliderVelocityField.name = "yet_another_fluid_sim_obstacle_collider_velocity";

    std::vector<glm::vec2> colliderData(
        to<std::size_t>(width) * height,
        glm::vec2{1.0f, eular::colliderTypeValue(eular::ColliderType::Sdf)});
    std::vector<glm::vec2> velocityData(to<std::size_t>(width) * height, glm::vec2{0.0f});

    for(auto i = 0u; i < 2; ++i) {
        textures::create(device, obstacleColliderField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT,
                         colliderData.data(), {width, height, 1u}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(glm::vec2));
        textures::create(device, obstacleColliderVelocityField[i], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32_SFLOAT,
                         velocityData.data(), {width, height, 1u}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(glm::vec2));
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
        if(write.pImageInfo) delete write.pImageInfo;
    }

    const std::array<eular::Collider, 1> colliders{{
        {obstacleColliderField.descriptorSet[eular::in], obstacleColliderVelocityField.descriptorSet[eular::in]}
    }};
    solver->setColliders(colliders);
}

void YetAnotherFluidSim::releaseObstacleColliderDescriptorSets() {
    releaseFieldDescriptorSets(obstacleColliderField);
    releaseFieldDescriptorSets(obstacleColliderVelocityField);
}

void YetAnotherFluidSim::releaseDescriptorSet(VkDescriptorSet& descriptorSet) {
    if(descriptorSet == VK_NULL_HANDLE) {
        return;
    }

    descriptorPool.free(descriptorSet);
    descriptorSet = VK_NULL_HANDLE;
}

void YetAnotherFluidSim::releaseFieldDescriptorSets(eular::Field& field) {
    releaseDescriptorSet(field.descriptorSet[eular::in]);
    releaseDescriptorSet(field.descriptorSet[eular::out]);
}

uint32_t YetAnotherFluidSim::createFieldDescriptorSet(std::vector<VkWriteDescriptorSet>& writes, uint32_t writeOffset, eular::Field& field) {
    auto sets = descriptorPool.allocate({solver->fieldDescriptorSetLayout(), solver->fieldDescriptorSetLayout()});

    field.descriptorSet[0] = sets[0];
    field.descriptorSet[1] = sets[1];

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 0;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 1;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[0];
    writes[writeOffset].dstBinding = 2;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 0;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 1;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    writes[writeOffset].dstSet = field.descriptorSet[1];
    writes[writeOffset].dstBinding = 2;
    writes[writeOffset].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[writeOffset].descriptorCount = 1;
    writes[writeOffset].pImageInfo = new VkDescriptorImageInfo{VK_NULL_HANDLE, field[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    ++writeOffset;

    return writeOffset;
}

void YetAnotherFluidSim::updateObstacleCollider(VkCommandBuffer commandBuffer) {
    const std::array<VkDescriptorSet, 2> sets{
        obstacleColliderField.descriptorSet[eular::in],
        obstacleColliderVelocityField.descriptorSet[eular::in]
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("obstacle"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("obstacle"),
                            0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.layout("obstacle"), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(obstacleConstants), &obstacleConstants);
    vkCmdDispatch(commandBuffer, (solverGridSize.x + 31u) / 32u, (solverGridSize.y + 31u) / 32u, 1);

    Barriers::pushAndFlush(commandBuffer, obstacleColliderField[eular::in].image, DEFAULT_SUB_RANGE,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    Barriers::pushAndFlush(commandBuffer, obstacleColliderVelocityField[eular::in].image, DEFAULT_SUB_RANGE,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
}

void YetAnotherFluidSim::updatePaintSmoke(VkCommandBuffer commandBuffer) {
    if(scene != Scene::Paint) {
        return;
    }

    const std::array<VkDescriptorSet, 2> sets{
        smoke.field.descriptorSet[eular::in],
        obstacleColliderField.descriptorSet[eular::in]
    };

    paintSmokeSourceConstants.frame = static_cast<uint32_t>(fixedUpdate.frames());

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("paint_smoke_source"));
    vkCmdPushConstants(commandBuffer, compute.layout("paint_smoke_source"), VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(paintSmokeSourceConstants), &paintSmokeSourceConstants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("paint_smoke_source"),
                            0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdDispatch(commandBuffer, (solverGridSize.x + 31u) / 32u, (solverGridSize.y + 31u) / 32u, 1);

    Barriers::pushAndFlush(commandBuffer, smoke.field[eular::in].image, DEFAULT_SUB_RANGE,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
}

void YetAnotherFluidSim::runSimulationStep(VkCommandBuffer commandBuffer) {
    const auto dt = fixedUpdate.period() > 0.0f ? fixedUpdate.period() : scenes[scene].timeStep;
    obstacleConstants.velocity = (obstacleConstants.position - obstacleSimulationPosition) / dt;
    obstacleSimulationPosition = obstacleConstants.position;
    forceConstants.point = obstacleConstants.position;
    forceConstants.velocity = obstacleConstants.velocity;
    forceConstants.domainMin = obstacleConstants.domainMin;
    forceConstants.domainMax = obstacleConstants.domainMax;
    forceConstants.radius = obstacleConstants.radius;
    forceConstants.dt = dt;

    updateObstacleCollider(commandBuffer);
    updatePaintSmoke(commandBuffer);
    solver->runSimulation(commandBuffer);
}

void YetAnotherFluidSim::initVisualizer() {
    visualizer.releaseDescriptorSets();

    visualizer = FieldVisualizer{
        &device, &descriptorPool, &renderPass, solver->fieldDescriptorSetLayout(),
        { width, height }, solverGridSize
    };

    visualizer.setDomain(domainSize);
    visualizer.setBoundaryColor(glm::vec4{0.0f, 0.0f, 0.0f, 1.0f});
    visualizer.setBoundaryWidth(1.0f);
    visualizer.init();
    visualizer.set(solver.get());
    visualizer.initFieldDumpReadback(fs::current_path() / "yet_another_fluid_sim" / "debug_dumps");
}

void YetAnotherFluidSim::initSmoke() {
    smokeField.assign(solverGridSize.x * solverGridSize.y, 0.0f);
    windTunnelSmokeSource.assign(solverGridSize.x * solverGridSize.y, 0.0f);

    if(scene == Scene::WindTunnel) {
        const auto pipeH = 0.1 * solverGridSize.y;
        const auto minJ = to<int>(0.5f * to<float>(solverGridSize.y) - 0.5f*pipeH);
        const auto maxJ = to<int>(0.5f * to<float>(solverGridSize.y) + 0.5f*pipeH);
        const auto sourceRate = 1.0f / scenes[scene].timeStep;

        constexpr auto inletColumn = 1;
        for (auto j = minJ; j < maxJ; ++j) {
            windTunnelSmokeSource[j * solverGridSize.x + inletColumn] = sourceRate;
        }

        const auto byteSize = to<VkDeviceSize>(windTunnelSmokeSource.size() * sizeof(float));
        windTunnelSmokeSourceUploadBuffer = device.createStagingBuffer(byteSize);
        windTunnelSmokeSourceUploadBuffer.copy(windTunnelSmokeSource.data(), byteSize);

        smoke.update = [&](VkCommandBuffer commandBuffer, eular::Field& field, glm::uvec3) {
            uploadCpuField(commandBuffer, windTunnelSmokeSourceUploadBuffer, field);
        };
    } else {
        smoke.update = [](VkCommandBuffer, eular::Field&, glm::uvec3) {};
    }

    smoke.diffuseRate = 0;
}

eular::ExternalForce YetAnotherFluidSim::force() {
    return [&](VkCommandBuffer commandBuffer, std::span<VkDescriptorSet> forceFieldSets, glm::uvec3 gc){
        auto& vectorField = solver->vectorField();
        const std::array<VkDescriptorSet, 5> sets{
            forceFieldSets[0],
            forceFieldSets[1],
            solver->colliderField().descriptorSet[eular::in],
            vectorField.u.descriptorSet[eular::in],
            vectorField.v.descriptorSet[eular::in]
        };

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("force"));
        vkCmdPushConstants(commandBuffer, compute.layout("force"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(forceConstants), &forceConstants);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("force"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdDispatch(commandBuffer,  gc.x, gc.y, gc.z);
    };
}

void YetAnotherFluidSim::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3ColorBlendEnable = VK_TRUE;
    dsFeatures->extendedDynamicState3ColorBlendEquation = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void YetAnotherFluidSim::createDescriptorPool() {
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


void YetAnotherFluidSim::createDescriptorSetLayouts() {
}

void YetAnotherFluidSim::updateDescriptorSets(){
}

void YetAnotherFluidSim::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void YetAnotherFluidSim::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void YetAnotherFluidSim::createComputePipeline() {
    forceFieldSetLayouts = solver->forceFieldSetLayouts();
    forceFieldSetLayouts.push_back(solver->fieldDescriptorSetLayout());
    forceFieldSetLayouts.push_back(solver->fieldDescriptorSetLayout());
    forceFieldSetLayouts.push_back(solver->fieldDescriptorSetLayout());
    obstacleColliderSetLayouts = {
        solver->fieldDescriptorSetLayout(),
        solver->fieldDescriptorSetLayout()
    };
    paintSmokeSetLayouts = {
        solver->fieldDescriptorSetLayout(),
        solver->fieldDescriptorSetLayout()
    };
    auto forceRange = VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(forceConstants)};
    auto obstacleRange = VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(obstacleConstants)};
    auto paintSmokeSourceRange = VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(paintSmokeSourceConstants)};

    compute = ComputePipelines{&device, {
        {
            "force",
            resource("force_generator.comp.spv"),
            {
                &forceFieldSetLayouts[0],
                &forceFieldSetLayouts[1],
                &forceFieldSetLayouts[2],
                &forceFieldSetLayouts[3],
                &forceFieldSetLayouts[4],
            },
            {forceRange}
        },
        {
            "obstacle",
            resource("obstacle.comp.spv"),
            {&obstacleColliderSetLayouts[0], &obstacleColliderSetLayouts[1]},
            {obstacleRange}
        },
        {
            "paint_smoke_source",
            resource("paint_smoke_source.comp.spv"),
            {&paintSmokeSetLayouts[0], &paintSmokeSetLayouts[1]},
            {paintSmokeSourceRange}
        }
    }};
    compute.createPipelines();
}


void YetAnotherFluidSim::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("render.frag.spv"))
                .vertexInputState()
                    .clear()
                .inputAssemblyState()
                    .triangleStrip()
                .rasterizationState()
                    .cullNone()
                .dynamicState()
                    .colorBlendEnable()
                    .colorBlendEquation()
                .depthStencilState()
                    .compareOpAlways()
                .layout()
                    .clear()
                    .addDescriptorSetLayout(solver->fieldDescriptorSetLayout())
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(obstacleConstants))
                .name("render")
                .build(render.layout);

        obstacleRender.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("obstacle.vert.spv"))
                    .fragmentShader(resource("obstacle.frag.spv"))
                .vertexInputState()
                    .clear()
                .inputAssemblyState()
                    .points()
                .rasterizationState()
                    .cullNone()
                .depthStencilState()
                    .compareOpAlways()
                .layout()
                    .clear()
                    .addPushConstantRange(VK_SHADER_STAGE_ALL, 0, sizeof(obstacleConstants))
                .name("obstacle_render")
                .build(obstacleRender.layout);
    //    @formatter:on
}


void YetAnotherFluidSim::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(obstacleRender.pipeline);
}

void YetAnotherFluidSim::onSwapChainRecreation() {
    scene = newScene;
    initSolver();
    createComputePipeline();
    createRenderPipeline();
}

VkCommandBuffer *YetAnotherFluidSim::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    visualizer.writePendingFieldDump();

    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(1, 0, 0);

    fixedUpdate([&] {
        runSimulationStep(commandBuffer);
    });

    // uploadCpuFields(commandBuffer);
    visualizer.update(commandBuffer);

    renderToSwapChain([&]{
        // visualizer.renderDebugFields(commandBuffer);
        if (showPressure) {
            obstacleConstants.color = glm::vec4(0);
            visualizer.renderPressure(commandBuffer);
        } else {
            obstacleConstants.color = scenes[scene].color;
        }


        renderSmoke(commandBuffer);
        // visualizer.renderVectorField(commandBuffer);
        // visualizer.renderStreamLines(commandBuffer);
        visualizer.renderBoundary(commandBuffer);
        renderObstacle(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void YetAnotherFluidSim::renderSmoke(VkCommandBuffer commandBuffer) {
    if (!showSmoke) return;
    const auto set = smoke.field.descriptorSet[eular::in];
    if(set == VK_NULL_HANDLE) {
        return;
    }

    VkBool32 enable = VK_TRUE;
    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &enable);

    static VkColorBlendEquationEXT blend{};
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp =  VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;

    if (showPressure) {
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.colorBlendOp =  VK_BLEND_OP_REVERSE_SUBTRACT;
    }

    vkCmdSetColorBlendEquationEXT(commandBuffer, 0, 1, &blend);


    auto constants = obstacleConstants;
    constants.scene = static_cast<uint32_t>(scene);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(constants), &constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle,
                            0, 1, &set, 0, VK_NULL_HANDLE);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);

    enable = VK_FALSE;
    vkCmdSetColorBlendEnableEXT(commandBuffer, 0, 1, &enable);
}

void YetAnotherFluidSim::renderObstacle(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obstacleRender.pipeline.handle);
    vkCmdPushConstants(commandBuffer, obstacleRender.layout.handle, VK_SHADER_STAGE_ALL, 0, sizeof(obstacleConstants), &obstacleConstants);
    vkCmdDraw(commandBuffer, 1, 1, 0, 0);
}

void YetAnotherFluidSim::renderUI(VkCommandBuffer cmdBuf) {
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({});

    if (ImGui::Button("Wind Tunnel")) {
        newScene = Scene::WindTunnel;
    }
    ImGui::SameLine();

    if (ImGui::Button("Tank")) {
        newScene = Scene::Tank;
    }
    ImGui::SameLine();

    if (ImGui::Button("Paint")) {
        newScene = Scene::Paint;
    }
    ImGui::SameLine();

    ImGui::Checkbox("Streamlines", &showStreamLines);
    ImGui::SameLine();

    ImGui::Checkbox("Pressure", &showPressure);
    ImGui::SameLine();

    ImGui::Checkbox("Smoke", &showSmoke);
    ImGui::SameLine();


    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(cmdBuf);
}

void YetAnotherFluidSim::update(float time) {
    auto title = fmt::format("{}, obstacle {}, fps {}", this->title, obstacleConstants.position, framePerSecond);
    glfwSetWindowTitle(window, title.c_str());
    fixedUpdate.advance(time);
    camera->update(time);
    auto cam = camera->cam();
    // updateCpuSolver();
}

void YetAnotherFluidSim::checkAppInputs() {
    camera->processInput();

    auto& o = obstacleConstants;
    const auto pos3 = mousePositionToWorldSpace({ .proj = obstacleConstants.transform});
    const auto mousePosition = glm::vec2(pos3);
    const auto leftPressed = mouse.left.held && !leftMouseWasHeld;

    if(leftPressed) {
        const auto dist = glm::distance(mousePosition, o.position) - o.radius;
        obstacleDragActive = dist < 0.0f;
        obstacleDragOffset = obstacleDragActive ? o.position - mousePosition : glm::vec2{0.0f};
    }

    if(mouse.left.held && obstacleDragActive) {
        o.position = mousePosition + obstacleDragOffset;
    }

    if(mouse.left.released || !mouse.left.held) {
        obstacleDragActive = false;
    }

    leftMouseWasHeld = mouse.left.held;
}

void YetAnotherFluidSim::cleanup() {
    visualizer.releaseDescriptorSets();
    releaseObstacleColliderDescriptorSets();
    solver.reset();
    AppContext::shutdown();
}

void YetAnotherFluidSim::onPause() {
    VulkanBaseApp::onPause();
}

void YetAnotherFluidSim::endFrame() {
    if (scene != newScene) {
        scene = newScene;
        invalidateSwapChain();
    }
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1900;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = false;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = YetAnotherFluidSim{ settings };

        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
