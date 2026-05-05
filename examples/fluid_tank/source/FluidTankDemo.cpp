#include "FluidTankDemo.hpp"

#include "Barrier.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "ExtensionChain.hpp"

namespace {
    constexpr uint32_t In = 0;
    constexpr uint32_t Out = 1;
    constexpr glm::uvec3 GroupSize{4, 4, 4};
    constexpr glm::uvec3 GridSize{48, 64, 48};

    glm::uvec3 dispatchCount() {
        return {
            (GridSize.x + GroupSize.x - 1) / GroupSize.x,
            (GridSize.y + GroupSize.y - 1) / GroupSize.y,
            (GridSize.z + GroupSize.z - 1) / GroupSize.z
        };
    }

    template<typename T>
    void uploadTextureData(const VulkanDevice& device, Texture& texture, const std::vector<T>& data) {
        auto staging = device.createCpuVisibleBuffer(data.data(), data.size() * sizeof(T), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        device.graphicsCommandPool().oneTimeCommand([&](VkCommandBuffer commandBuffer) {
            texture.image.copyFromBuffer(commandBuffer, staging, texture.image.currentLayout);
        });
    }
}

FluidTankDemo::FluidTankDemo(const Settings& settings)
    : VulkanBaseApp("Fluid Tank", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("fluid_tank");
    fileManager().addSearchPathFront("fluid_tank/data");
    fileManager().addSearchPathFront("fluid_tank/spv");
    fileManager().addSearchPathFront("fluid_tank/models");
    fileManager().addSearchPathFront("fluid_tank/textures");
}

void FluidTankDemo::initApp() {
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initCamera();
    createTextures();
    initDensityField();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipelines();
    updateSimulationUniforms();
    updateRenderUniforms();
}

void FluidTankDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void FluidTankDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 128;
    std::array<VkDescriptorPoolSize, 3> poolSizes{{
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8 * maxSets},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8 * maxSets},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * maxSets}
    }};
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void FluidTankDemo::initCamera() {
    OrbitingCameraSettings cameraSettings{};
    cameraSettings.offsetDistance = 2.0f;
    cameraSettings.rotationSpeed = 0.15f;
    cameraSettings.fieldOfView = 45.0f;
    cameraSettings.modelHeight = 0.5f;
    cameraSettings.aspectRatio = float(swapChain.extent.width) / float(swapChain.extent.height);
    cameraSettings.zNear = 0.05f;
    cameraSettings.zFar = 20.0f;
    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void FluidTankDemo::createTextures() {
    const glm::uvec3 size{GridX, GridY, GridZ};

    auto createScalar = [&](Texture& texture, const std::string& name) {
        textures::createNoTransition(device, texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
        device.setName<VK_OBJECT_TYPE_IMAGE>(name, texture.image.image);
    };

    auto createVector = [&](Texture& texture, const std::string& name) {
        textures::createNoTransition(device, texture, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
        device.setName<VK_OBJECT_TYPE_IMAGE>(name, texture.image.image);
    };

    createVector(velocity.tex[0], "fluid_tank_velocity_0");
    createVector(velocity.tex[1], "fluid_tank_velocity_1");
    createScalar(density.tex[0], "fluid_tank_density_0");
    createScalar(density.tex[1], "fluid_tank_density_1");
    createScalar(pressure.tex[0], "fluid_tank_pressure_0");
    createScalar(pressure.tex[1], "fluid_tank_pressure_1");
    createScalar(divergence, "fluid_tank_divergence");
    createScalar(obstacle, "fluid_tank_obstacle");
    createScalar(densityForward, "fluid_tank_density_forward");
    createScalar(densityBackward, "fluid_tank_density_backward");

    textures::create(device, dummyScalar, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, std::vector<float>(GridX * GridY * GridZ, 0.0f).data(),
                     size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    dummyScalar.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::create(device, dummyVector, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT,
                     std::vector<glm::vec4>(GridX * GridY * GridZ, glm::vec4(0.0f)).data(),
                     size, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    dummyVector.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void FluidTankDemo::initDensityField() {
    std::vector<float> initialDensity(GridX * GridY * GridZ, 0.0f);
    for (uint32_t z = 0; z < GridZ; ++z) {
        for (uint32_t y = 0; y < GridY; ++y) {
            for (uint32_t x = 0; x < GridX; ++x) {
                const float fy = (float(y) + 0.5f) / float(GridY);
                if (fy < options.fillLevel) {
                    initialDensity[x + GridX * (y + GridY * z)] = 1.0f;
                }
            }
        }
    }

    uploadTextureData(device, density.tex[0], initialDensity);
    uploadTextureData(device, density.tex[1], initialDensity);
}

void FluidTankDemo::createDescriptorSetLayouts() {
    simulationSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fluid_tank_simulation_uniforms")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    computeSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fluid_tank_compute_resources")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(5)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    renderSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fluid_tank_render_resources")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
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
}

void FluidTankDemo::updateDescriptorSets() {
    SimulationUniform simDefaults{};
    simulationUniformBuffer = device.createCpuVisibleBuffer(&simDefaults, sizeof(simDefaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    simulationUniform = reinterpret_cast<SimulationUniform*>(simulationUniformBuffer.map());

    RenderUniform renderDefaults{};
    renderUniformBuffer = device.createCpuVisibleBuffer(&renderDefaults, sizeof(renderDefaults), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    renderUniform = reinterpret_cast<RenderUniform*>(renderUniformBuffer.map());

    auto writeComputeSet = [&](VkDescriptorSet set, Texture& velocityTex, Texture& sourceTex, Texture& pressureTex,
                               Texture& divergenceTex, Texture& obstacleTex, Texture& outTex) {
        auto writes = initializers::writeDescriptorSets<6>();

        VkDescriptorImageInfo velocityInfo{velocityTex.sampler.handle, velocityTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo sourceInfo{sourceTex.sampler.handle, sourceTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo pressureInfo{pressureTex.sampler.handle, pressureTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo divergenceInfo{divergenceTex.sampler.handle, divergenceTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo obstacleInfo{obstacleTex.sampler.handle, obstacleTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo outInfo{VK_NULL_HANDLE, outTex.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};

        for (uint32_t i = 0; i < 5; ++i) {
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].descriptorCount = 1;
        }
        writes[0].pImageInfo = &velocityInfo;
        writes[1].pImageInfo = &sourceInfo;
        writes[2].pImageInfo = &pressureInfo;
        writes[3].pImageInfo = &divergenceInfo;
        writes[4].pImageInfo = &obstacleInfo;

        writes[5].dstSet = set;
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[5].descriptorCount = 1;
        writes[5].pImageInfo = &outInfo;
        device.updateDescriptorSets(writes);
    };

    auto alloc = descriptorPool.allocate({
        simulationSetLayout,
        computeSetLayout, computeSetLayout,
        computeSetLayout, computeSetLayout,
        computeSetLayout,
        computeSetLayout, computeSetLayout,
        computeSetLayout, computeSetLayout,
        computeSetLayout,
        renderSetLayout
    });

    uint32_t index = 0;
    simulationUniformSet = alloc[index++];
    advectVelocitySet[In] = alloc[index++];
    advectVelocitySet[Out] = alloc[index++];
    applyForcesSet[In] = alloc[index++];
    applyForcesSet[Out] = alloc[index++];
    divergenceSet = alloc[index++];
    jacobiSet[In] = alloc[index++];
    jacobiSet[Out] = alloc[index++];
    projectSet[In] = alloc[index++];
    projectSet[Out] = alloc[index++];
    obstacleSet = alloc[index++];
    renderSet = alloc[index++];

    auto uniformWrite = initializers::writeDescriptorSets<1>();
    VkDescriptorBufferInfo simInfo{simulationUniformBuffer, 0, VK_WHOLE_SIZE};
    uniformWrite[0].dstSet = simulationUniformSet;
    uniformWrite[0].dstBinding = 0;
    uniformWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite[0].descriptorCount = 1;
    uniformWrite[0].pBufferInfo = &simInfo;
    device.updateDescriptorSets(uniformWrite);

    writeComputeSet(advectVelocitySet[In], velocity.tex[In], velocity.tex[In], dummyScalar, dummyScalar, obstacle, velocity.tex[Out]);
    writeComputeSet(advectVelocitySet[Out], velocity.tex[Out], velocity.tex[Out], dummyScalar, dummyScalar, obstacle, velocity.tex[In]);

    writeComputeSet(applyForcesSet[In], velocity.tex[Out], density.tex[In], dummyScalar, dummyScalar, obstacle, velocity.tex[In]);
    writeComputeSet(applyForcesSet[Out], velocity.tex[In], density.tex[Out], dummyScalar, dummyScalar, obstacle, velocity.tex[Out]);

    writeComputeSet(divergenceSet, velocity.tex[In], dummyScalar, dummyScalar, dummyScalar, obstacle, divergence);

    writeComputeSet(jacobiSet[In], dummyScalar, dummyScalar, pressure.tex[In], divergence, obstacle, pressure.tex[Out]);
    writeComputeSet(jacobiSet[Out], dummyScalar, dummyScalar, pressure.tex[Out], divergence, obstacle, pressure.tex[In]);

    writeComputeSet(projectSet[In], velocity.tex[In], dummyScalar, pressure.tex[In], dummyScalar, obstacle, velocity.tex[Out]);
    writeComputeSet(projectSet[Out], velocity.tex[Out], dummyScalar, pressure.tex[Out], dummyScalar, obstacle, velocity.tex[In]);

    writeComputeSet(obstacleSet, dummyScalar, dummyScalar, dummyScalar, dummyScalar, dummyScalar, obstacle);

    auto densityForwardAlloc = descriptorPool.allocate({computeSetLayout, computeSetLayout});
    densityForwardSet[In] = densityForwardAlloc.at(0);
    densityForwardSet[Out] = densityForwardAlloc.at(1);
    writeComputeSet(densityForwardSet[In], velocity.tex[In], density.tex[In], dummyScalar, dummyScalar, obstacle, densityForward);
    writeComputeSet(densityForwardSet[Out], velocity.tex[Out], density.tex[Out], dummyScalar, dummyScalar, obstacle, densityForward);

    auto densityReverseAlloc = descriptorPool.allocate({computeSetLayout, computeSetLayout});
    densityReverseSet[In] = densityReverseAlloc.at(0);
    densityReverseSet[Out] = densityReverseAlloc.at(1);
    writeComputeSet(densityReverseSet[In], velocity.tex[In], densityForward, dummyScalar, dummyScalar, obstacle, densityBackward);
    writeComputeSet(densityReverseSet[Out], velocity.tex[Out], densityForward, dummyScalar, dummyScalar, obstacle, densityBackward);

    auto densityCorrectAlloc = descriptorPool.allocate({computeSetLayout, computeSetLayout});
    densityCorrectSet[In] = densityCorrectAlloc.at(0);
    densityCorrectSet[Out] = densityCorrectAlloc.at(1);
    writeComputeSet(densityCorrectSet[In], velocity.tex[In], density.tex[In], densityForward, densityBackward, obstacle, density.tex[Out]);
    writeComputeSet(densityCorrectSet[Out], velocity.tex[Out], density.tex[Out], densityForward, densityBackward, obstacle, density.tex[In]);

    auto renderWrites = initializers::writeDescriptorSets<3>();
    VkDescriptorBufferInfo renderInfo{renderUniformBuffer, 0, VK_WHOLE_SIZE};
    VkDescriptorImageInfo densityInfo{density.tex[In].sampler.handle, density.tex[In].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo obstacleInfo{obstacle.sampler.handle, obstacle.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    renderWrites[0].dstSet = renderSet;
    renderWrites[0].dstBinding = 0;
    renderWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    renderWrites[0].descriptorCount = 1;
    renderWrites[0].pBufferInfo = &renderInfo;
    renderWrites[1].dstSet = renderSet;
    renderWrites[1].dstBinding = 1;
    renderWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    renderWrites[1].descriptorCount = 1;
    renderWrites[1].pImageInfo = &densityInfo;
    renderWrites[2].dstSet = renderSet;
    renderWrites[2].dstBinding = 2;
    renderWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    renderWrites[2].descriptorCount = 1;
    renderWrites[2].pImageInfo = &obstacleInfo;
    device.updateDescriptorSets(renderWrites);
}

void FluidTankDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void FluidTankDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void FluidTankDemo::createRenderPipeline() {
    render.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("tank_render.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayout(renderSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Camera))
            .name("fluid_tank_render")
        .build(render.layout);
}

void FluidTankDemo::createComputePipelines() {
    compute = ComputePipelines(&device, {
        {
            .name = "obstacle",
            .shadePath = resource("obstacle.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        },
        {
            .name = "advect_vec4",
            .shadePath = resource("advect_vec4.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float) } }
        },
        {
            .name = "advect_scalar",
            .shadePath = resource("advect_scalar.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float) } }
        },
        {
            .name = "apply_forces",
            .shadePath = resource("apply_forces.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        },
        {
            .name = "divergence",
            .shadePath = resource("divergence.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        },
        {
            .name = "jacobi",
            .shadePath = resource("jacobi.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        },
        {
            .name = "project",
            .shadePath = resource("project.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        },
        {
            .name = "maccormack",
            .shadePath = resource("maccormack.comp.spv"),
            .layouts = { &simulationSetLayout, &computeSetLayout }
        }
    });
    compute.createPipelines();
}

void FluidTankDemo::updateSimulationUniforms() {
    simulationUniform->gridSize = glm::ivec4(GridX, GridY, GridZ, options.pressureIterations);
    simulationUniform->invGridSizeDt = glm::vec4(1.0f / GridX, 1.0f / GridY, 1.0f / GridZ, (1.0f / 60.0f) * options.timeScale);
    simulationUniform->spherePosRadius = glm::vec4(sphere.position, sphere.radius);
    simulationUniform->sphereVelocity = glm::vec4(sphere.velocity, 0.0f);
    simulationUniform->fluidParams = glm::vec4(options.fillLevel, options.gravity, options.splash, options.dissipation);
}

void FluidTankDemo::updateRenderUniforms() {
    renderUniform->waterColor = glm::vec4(options.waterColor, 0.0f);
    renderUniform->renderParams = glm::vec4(options.threshold, options.absorption, options.stepScale, options.glass);
}

void FluidTankDemo::updateSphere(float dt) {
    if (sphere.resetRequested) {
        sphere.position = glm::vec3{0.5f, 1.08f, 0.5f};
        sphere.velocity = glm::vec3{0.0f};
        sphere.resetRequested = false;
    }

    sphere.velocity.y += options.gravity * dt * 0.7f;
    sphere.position += sphere.velocity * dt;

    if (sphere.position.y < sphere.radius) {
        sphere.position.y = sphere.radius;
        sphere.velocity.y *= -0.35f;
    }

    if (sphere.position.y > 1.12f) {
        sphere.position.y = 1.12f;
        sphere.velocity.y = 0.0f;
    }
}

void FluidTankDemo::simulate() {
    const auto gc = dispatchCount();
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        auto bindAndDispatch = [&](const char* pipelineName, VkDescriptorSet resources, const void* pushData = nullptr, uint32_t pushSize = 0) {
            std::array<VkDescriptorSet, 2> sets{simulationUniformSet, resources};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline(pipelineName));
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout(pipelineName), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
            if (pushData != nullptr && pushSize > 0) {
                vkCmdPushConstants(commandBuffer, compute.layout(pipelineName), VK_SHADER_STAGE_COMPUTE_BIT, 0, pushSize, pushData);
            }
            vkCmdDispatch(commandBuffer, gc.x, gc.y, gc.z);
            Barrier::computeWriteToRead(commandBuffer);
        };

        bindAndDispatch("obstacle", obstacleSet);

        const float forward = 1.0f;
        bindAndDispatch("advect_vec4", advectVelocitySet[In], &forward, sizeof(float));
        bindAndDispatch("apply_forces", applyForcesSet[In]);
        std::swap(velocity.tex[In], velocity.tex[Out]);
        std::swap(applyForcesSet[In], applyForcesSet[Out]);
        std::swap(advectVelocitySet[In], advectVelocitySet[Out]);
        std::swap(projectSet[In], projectSet[Out]);
        std::swap(densityForwardSet[In], densityForwardSet[Out]);
        std::swap(densityReverseSet[In], densityReverseSet[Out]);
        std::swap(densityCorrectSet[In], densityCorrectSet[Out]);

        bindAndDispatch("divergence", divergenceSet);

        for (int i = 0; i < options.pressureIterations; ++i) {
            bindAndDispatch("jacobi", jacobiSet[In]);
            std::swap(pressure.tex[In], pressure.tex[Out]);
            std::swap(jacobiSet[In], jacobiSet[Out]);
        }

        bindAndDispatch("project", projectSet[In]);
        std::swap(velocity.tex[In], velocity.tex[Out]);
        std::swap(advectVelocitySet[In], advectVelocitySet[Out]);
        std::swap(applyForcesSet[In], applyForcesSet[Out]);
        std::swap(projectSet[In], projectSet[Out]);
        std::swap(densityForwardSet[In], densityForwardSet[Out]);
        std::swap(densityReverseSet[In], densityReverseSet[Out]);
        std::swap(densityCorrectSet[In], densityCorrectSet[Out]);

        bindAndDispatch("advect_scalar", densityForwardSet[In], &forward, sizeof(float));
        const float reverse = -1.0f;
        bindAndDispatch("advect_scalar", densityReverseSet[In], &reverse, sizeof(float));
        bindAndDispatch("maccormack", densityCorrectSet[In]);
        std::swap(density.tex[In], density.tex[Out]);
        std::swap(densityForwardSet[In], densityForwardSet[Out]);
        std::swap(densityReverseSet[In], densityReverseSet[Out]);
        std::swap(densityCorrectSet[In], densityCorrectSet[Out]);
    });

    auto renderWrites = initializers::writeDescriptorSets<1>();
    VkDescriptorImageInfo densityInfo{density.tex[In].sampler.handle, density.tex[In].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    renderWrites[0].dstSet = renderSet;
    renderWrites[0].dstBinding = 1;
    renderWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    renderWrites[0].descriptorCount = 1;
    renderWrites[0].pImageInfo = &densityInfo;
    device.updateDescriptorSets(renderWrites);
}

void FluidTankDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Fluid Tank");
    ImGui::SetWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    ImGui::Checkbox("Run", &options.running);
    if (ImGui::Button("Reset Ball")) {
        sphere.resetRequested = true;
    }
    ImGui::SliderFloat("Time Scale", &options.timeScale, 0.1f, 2.0f);
    ImGui::SliderInt("Pressure Iterations", &options.pressureIterations, 8, 48);
    ImGui::SliderFloat("Fill Level", &options.fillLevel, 0.2f, 0.8f);
    ImGui::SliderFloat("Gravity", &options.gravity, -1.2f, -0.1f);
    ImGui::SliderFloat("Splash", &options.splash, 0.5f, 8.0f);
    ImGui::SliderFloat("Dissipation", &options.dissipation, 0.96f, 1.0f);
    ImGui::Separator();
    ImGui::SliderFloat("Threshold", &options.threshold, 0.02f, 0.3f);
    ImGui::SliderFloat("Absorption", &options.absorption, 0.5f, 8.0f);
    ImGui::SliderFloat("Step Scale", &options.stepScale, 0.005f, 0.04f);
    ImGui::SliderFloat("Glass", &options.glass, 0.0f, 1.0f);
    ImGui::ColorEdit3("Water Color", &options.waterColor.x);
    ImGui::Text("Ball y: %.3f  vy: %.3f", sphere.position.y, sphere.velocity.y);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void FluidTankDemo::onSwapChainDispose() {
    dispose(render.pipeline);
}

void FluidTankDemo::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    createRenderPipeline();
}

VkCommandBuffer* FluidTankDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0.02f, 0.03f, 0.05f);

    renderToSwapChain([&] {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &renderSet, 0, nullptr);
        camera->push(commandBuffer, render.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        AppContext::renderClipSpaceQuad(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);
    return &commandBuffer;
}

void FluidTankDemo::update(float time) {
    if (!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }

    if (options.running) {
        updateSphere((1.0f / 60.0f) * options.timeScale);
        updateSimulationUniforms();
        simulate();
    }
    updateRenderUniforms();
}

void FluidTankDemo::checkAppInputs() {
    camera->processInput();
}

void FluidTankDemo::cleanup() {
    AppContext::shutdown();
}

void FluidTankDemo::onPause() {
    VulkanBaseApp::onPause();
}

int main() {
    try {
        fs::current_path("../../../../examples/");

        Settings settings;
        settings.width = 1600;
        settings.height = 900;
        settings.depthTest = false;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = FluidTankDemo{settings};
        app.addPlugin(imGui);
        app.run();
    } catch (std::runtime_error& err) {
        spdlog::error(err.what());
    }
}
