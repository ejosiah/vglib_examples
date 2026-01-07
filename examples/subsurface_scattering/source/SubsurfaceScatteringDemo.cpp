#include "SubsurfaceScatteringDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

SubsurfaceScatteringDemo::SubsurfaceScatteringDemo(const Settings& settings) : VulkanBaseApp("Separable Subsurface Scattering", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/textures/environment");
    fileManager().addSearchPathFront("../data/textures/lut");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("subsurface_scattering");
    fileManager().addSearchPathFront("subsurface_scattering/data");
    fileManager().addSearchPathFront("subsurface_scattering/spv");
    fileManager().addSearchPathFront("subsurface_scattering/models");
    fileManager().addSearchPathFront("subsurface_scattering/textures");
}

void SubsurfaceScatteringDemo::initApp() {
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    loadModel();
    initCamera();
    initUniforms();
    initShadowMap();
    initGBuffers();
    createSkybox();
    loadEnvironment();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initCompute();
    initTaa();
}

void SubsurfaceScatteringDemo::initTaa() {
    jitter.sampler.type = static_cast<SamplerType>(SamplerType::Interleaved_Gradients);
    jitter.period(4);

    taa::Settings taa_settings{};
    taa_settings.resolution = { width, height};
    taa = std::make_unique<taa::Taa>(device, descriptorPool, bindlessDescriptor, gbuffer.color, gbuffer.depth, *camera, jitterValue, taa_settings);
    taa->init();
}

void SubsurfaceScatteringDemo::initCompute() {
    compute = ComputePipelines(&device, {{
        .name = "sss_blur",
        .shadePath = resource("sss_blur.comp.spv"),
        .layouts = { &uniformDescriptorSetLayout, const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout)},
        .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec2)} }
     }});
    compute.createPipelines();
}

void SubsurfaceScatteringDemo::initUniforms() {
    UniformData defaults{};
    uniforms.gpu = device.createCpuVisibleBuffer(&defaults, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData*>(uniforms.gpu.map());
    device.setName<VK_OBJECT_TYPE_BUFFER>("uniforms", uniforms.gpu.buffer);

    gltf::Light initialValues{};
    initialValues.position = glm::vec3{0, 0, 2};
    initialValues.intensity = 20;
    initialValues.innerConeCos = glm::cos(glm::radians(30.f));
    initialValues.outerConeCos = glm::cos(glm::radians(45.f));
    initialValues.type = to<int>(gltf::LightType::SPOT);
    light.gpu = device.createCpuVisibleBuffer(&initialValues, sizeof(gltf::Light), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    light.cpu = reinterpret_cast<gltf::Light*>(light.gpu.map());
    device.setName<VK_OBJECT_TYPE_BUFFER>("light", uniforms.gpu.buffer);

    uniforms.cpu->pixelSize = 1.f/glm::vec2(width, height);
    uniforms.cpu->near = 0.01;
    uniforms.cpu->far = 50;
}

void SubsurfaceScatteringDemo::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = model.height();
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void SubsurfaceScatteringDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void SubsurfaceScatteringDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void SubsurfaceScatteringDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 400;
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


void SubsurfaceScatteringDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void SubsurfaceScatteringDemo::createDescriptorSetLayouts() {
    environment.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("environment_descriptor_set_layout")
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
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    uniformDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("uniform_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void SubsurfaceScatteringDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({environment.descriptorSetLayout, uniformDescriptorSetLayout});
    environment.descriptorSet = sets[0];
    uniformDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<7>();

    writes[0].dstSet = environment.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo envMapInfo{ environment.albedo.sampler.handle, environment.albedo.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &envMapInfo;

    writes[1].dstSet = environment.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo specularInfo{ environment.specular.sampler.handle, environment.specular.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1].pImageInfo = &specularInfo;

    writes[2].dstSet = environment.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo irradianceInfo{ environment.irradiance.sampler.handle, environment.irradiance.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[2].pImageInfo = &irradianceInfo;

    writes[3].dstSet = environment.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo brdfInfo{ environment.brdfLut.sampler.handle, environment.brdfLut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[3].pImageInfo = &brdfInfo;

    writes[4].dstSet = environment.descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo beckmannInfo{ environment.beckmannLut.sampler.handle, environment.beckmannLut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[4].pImageInfo = &beckmannInfo;

    writes[5].dstSet = uniformDescriptorSet;
    writes[5].dstBinding = 0;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo uniformInfo{ uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[5].pBufferInfo = &uniformInfo;

    writes[6].dstSet = uniformDescriptorSet;
    writes[6].dstBinding = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[6].descriptorCount = 1;
    VkDescriptorBufferInfo lightInfo{ light.gpu, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &lightInfo;


    device.updateDescriptorSets(writes);
}

void SubsurfaceScatteringDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SubsurfaceScatteringDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SubsurfaceScatteringDemo::createRenderPipeline() {
    //    @formatter:off
    auto builder = prototypes->cloneGraphicsPipeline();
    render.lightingPass1.pipeline =
        builder
            .shaderStage()
                .vertexShader(resource("render.vert.spv"))
                .fragmentShader(resource("render.frag.spv"))
            .colorBlendState()
                .attachments(2)
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .layout()
                .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .addDescriptorSetLayout(environment.descriptorSetLayout)
                .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .addDescriptorSetLayout(model.descriptorSetLayout)
            .name("render")
            .build(render.lightingPass1.layout);

    render.shadowMap.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("shadowmap.vert.spv"))
                .fragmentShader(resource("shadowmap.frag.spv"))
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(shadowMap.size, shadowMap.size)
                    .minDepth(0)
                    .maxDepth(1)
                .scissor()
                    .offset(0, 0)
                    .extent(shadowMap.size, shadowMap.size)
                .add()
            .rasterizationState()
                .enableDepthBias()
                .depthBiasConstantFactor(shadowMap.depthBiasConstant)
                .depthBiasSlopeFactor(shadowMap.depthBiasSlope)
//                .cullFrontFace()
            .dynamicRenderPass()
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .layout().clear()
                .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4))
            .name("render")
            .build(render.shadowMap.layout);

    render.environment.pipeline =
        prototypes->cloneGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("environment.vert.spv"))
                .fragmentShader(resource("environment.frag.spv"))
            .vertexInputState().clear()
                .addVertexBindingDescription(0, sizeof(glm::vec3), VK_VERTEX_INPUT_RATE_VERTEX)
                .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0)
            .rasterizationState()
                .cullFrontFace()
            .depthStencilState()
                .compareOpLessOrEqual()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .layout()
                .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .addDescriptorSetLayout(environment.descriptorSetLayout)
            .name("environment")
        .build(render.environment.layout);

    render.lightingFinal.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("lighting.frag.spv"))
            .depthStencilState()
                .disableDepthTest()
                .disableDepthWrite()
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .layout().clear()
                .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
            .name("lighting_final")
        .build(render.lightingFinal.layout);

    render.tone_mapping.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("tone_mapping.frag.spv"))
            .layout().clear()
                .addDescriptorSetLayout(uniformDescriptorSetLayout)
                .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
            .name("tone_mapping")
        .build(render.tone_mapping.layout);
    //    @formatter:on
}


void SubsurfaceScatteringDemo::onSwapChainDispose() {
    dispose(render.lightingPass1.pipeline);
    dispose(render.lightingFinal.pipeline);
    dispose(render.environment.pipeline);
    dispose(render.tone_mapping.pipeline);
}

void SubsurfaceScatteringDemo::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

void SubsurfaceScatteringDemo::newFrame() {

    if(options.taaEnabled) {
        jitterValue = (-1.f + 2.f * jitter.nextSample()) * .5f;
        jitterValue /= glm::vec2(width, height);

        camera->newFrame();
        camera->jitter(jitterValue.x, jitterValue.y);
        taa->newFrame();
    }
}

VkCommandBuffer *SubsurfaceScatteringDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    captureShadow(commandBuffer);

    renderModel(commandBuffer);
    renderScene(commandBuffer);
    sssBlur(commandBuffer);

    if(options.taaEnabled) {
        taa->exec(commandBuffer);
    }

    renderToSwapChain([&]{
        toneMapp(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SubsurfaceScatteringDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({});
    ImGui::SliderFloat("Spec. intensity", &options.sIntensity, 0, 4);
    ImGui::SliderFloat("Spec. roughness", &options.sRoughness, 0, 1);
    ImGui::SliderFloat("Spec. fresnel", &options.sFresnel, 0, 1);
    ImGui::SliderFloat("Bumpiness", &options.bumpiness, 0, 1);
    ImGui::SliderFloat("Ambient", &options.ambientFactor, 0, 1);
    ImGui::Checkbox("Subsurface Scattering", &options.ssEnabled);
    if(options.ssEnabled) {
        ImGui::SliderFloat("Translucency", &options.translucency, 0, 1);
        ImGui::SliderFloat("Scattering radius", &options.scatteringRadius, 1, 40);
    }

    ImGui::Checkbox("Temporal AA", &options.taaEnabled);

    ImGui::SliderFloat2("Light direction", &options.lightDirection.x, -1, 1);
    ImGui::SliderFloat("Light Angle", &options.lightAngle, 0, 360);
    ImGui::SliderFloat("Light spot angle", &options.spotAngle, 1, 90);
    ImGui::SliderFloat("Env rotation", &options.envRotation, 0, 360);

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
};

void SubsurfaceScatteringDemo::renderModel(VkCommandBuffer commandBuffer) {
    offscreen.render(commandBuffer, lightingRenderInfo, [&]{
        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = uniformDescriptorSet;
        sets[1] = environment.descriptorSet;
        sets[2] = bindlessDescriptor.descriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lightingPass1.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lightingPass1.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        camera->push(commandBuffer, render.lightingPass1.layout);
        model.draw(commandBuffer, render.lightingPass1.layout, COUNT(sets));
    });
    Barrier::fragmentWriteToFragmentRead(commandBuffer);
}

void SubsurfaceScatteringDemo::renderEnvironment(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uniformDescriptorSet;
    sets[1] = environment.descriptorSet;

    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.environment.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.environment.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, render.environment.layout);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, skyBox.vertices, &offset);
    vkCmdBindIndexBuffer(commandBuffer, skyBox.indices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, skyBox.indices.sizeAs<uint32_t>(), 1, 0, 0, 0);
}

void SubsurfaceScatteringDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
}

void SubsurfaceScatteringDemo::checkAppInputs() {
    camera->processInput();
}

void SubsurfaceScatteringDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void SubsurfaceScatteringDemo::onPause() {
    VulkanBaseApp::onPause();
}

void SubsurfaceScatteringDemo::loadModel() {
    phong::VulkanDrawableInfo info{ .transform = glm::scale(glm::mat4{1}, glm::vec3(4)) };
    info.flipUv = true;
    phong::load2(resource("head/head_with_normals.obj"), device, descriptorPool, model, info);
}

void SubsurfaceScatteringDemo::loadEnvironment() {
    textures::fromFile(device, environment.brdfLut, resource("lut_ggx.png"));
    textures::fromFile(device, environment.beckmannLut, resource("lut_beckmann.png"));
    environment.albedo = textures::equirectangularToOctahedralMap(device, resource(environment.path), 2048);
    textures::ibl(device, environment.albedo, environment.irradiance, environment.specular);
}

void SubsurfaceScatteringDemo::createSkybox() {
    auto cube = primitives::cube();
    std::vector<glm::vec3> vertices;
    for(const auto& vertex : cube.vertices){
        vertices.push_back(vertex.position.xyz());
    }
    skyBox.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    skyBox.indices = device.createDeviceLocalBuffer(cube.indices.data(), BYTE_SIZE(cube.indices), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

void SubsurfaceScatteringDemo::endFrame() {
    uniforms.cpu->envRotation = glm::mat3(glm::rotate(glm::mat4{1}, glm::radians(options.envRotation), {0, 1, 0}));
    uniforms.cpu->sssWidth = options.scatteringRadius * 0.001f;
    uniforms.cpu->specularRoughness = options.sRoughness;
    uniforms.cpu->specularIntensity = options.sIntensity;
    uniforms.cpu->specularFresnel = options.sFresnel;
    uniforms.cpu->bumpiness = options.bumpiness;
    uniforms.cpu->ambientFactor = options.ambientFactor;
    uniforms.cpu->sss_enabled = options.ssEnabled;
    uniforms.cpu->translucency = options.translucency;

    auto v = options.lightDirection;
    auto lightRotation = glm::rotate(glm::mat4{1}, glm::radians(options.lightAngle), {0, 1, 0});
    light.cpu->position =  (lightRotation * glm::vec4(0, 0, 2, 1)).xyz();
    light.cpu->direction = (lightRotation * glm::vec4(v.x, v.y, -1, 0)).xyz();
    light.cpu->outerConeCos = glm::cos(glm::radians(options.spotAngle));
    light.cpu->innerConeCos = glm::cos(glm::radians(options.spotAngle * 0.5f));

    const auto fov = glm::acos(light.cpu->outerConeCos) * 2.f;
    const auto lPos = light.cpu->position;
    const auto target = lPos + light.cpu->direction;
    const auto near = uniforms.cpu->lightNearPlane;
    const auto far = uniforms.cpu->lightFarPlane;
    uniforms.cpu->lightSpaceMatrix = vkn::perspective(fov, 1.f, near, far) * glm::lookAt(lPos, target, {0, 1, 0});
    shadowMap.lightViewMatrix = uniforms.cpu->lightSpaceMatrix * camera->cam().model;

    taa->endFrame();
}

void SubsurfaceScatteringDemo::initGBuffers() {
    const auto width = swapChain.width();
    const auto height = swapChain.height();
    textures::create(device, gbuffer.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gbuffer.specular, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gbuffer.diffuse, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, gbuffer.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});
    textures::create(device, gbuffer.sssOutput, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});

    gbuffer.sssOutput.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    lightingRenderInfo = Offscreen::RenderInfo{
        .colorAttachments = { {gbuffer.specular.imageView, VK_FORMAT_R32G32B32A32_SFLOAT, glm::vec4(0)}, {gbuffer.diffuse.imageView, VK_FORMAT_R32G32B32A32_SFLOAT} },
        .depthAttachment = {{gbuffer.depth.imageView, VK_FORMAT_D16_UNORM}},
        .renderArea = {width, height}
    };

    renderInfo = Offscreen::RenderInfo{
        .colorAttachments = { {gbuffer.color.imageView, VK_FORMAT_R32G32B32A32_SFLOAT} },
        .depthAttachment = {{gbuffer.depth.imageView, VK_FORMAT_D16_UNORM}},
        .renderArea = {width, height}
    };
    renderInfo.depthAttachment->clear = false;

    uniforms.cpu->diffuse_tex_id = bindlessDescriptor.update(gbuffer.diffuse, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    uniforms.cpu->specular_tex_id = bindlessDescriptor.update(gbuffer.specular, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    uniforms.cpu->color_tex_id = bindlessDescriptor.update(gbuffer.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    uniforms.cpu->depth_tex_id = bindlessDescriptor.update(gbuffer.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    uniforms.cpu->sss_tex_id = bindlessDescriptor.update(gbuffer.sssOutput, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_IMAGE_LAYOUT_GENERAL);
    uniforms.cpu->sss_image_id = bindlessDescriptor.update(gbuffer.sssOutput, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
}

void SubsurfaceScatteringDemo::finalLighting(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uniformDescriptorSet;
    sets[1] = bindlessDescriptor.descriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lightingFinal.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.lightingFinal.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void SubsurfaceScatteringDemo::toneMapp(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uniformDescriptorSet;
    sets[1] = bindlessDescriptor.descriptorSet;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.tone_mapping.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.tone_mapping.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void SubsurfaceScatteringDemo::renderScene(VkCommandBuffer commandBuffer) {
    offscreen.render(commandBuffer, renderInfo, [&]{
        finalLighting(commandBuffer);
        renderEnvironment(commandBuffer);
    });
    Barriers::push(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
}

void SubsurfaceScatteringDemo::sssBlur(VkCommandBuffer commandBuffer) {
    if(!options.ssEnabled) return;

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = uniformDescriptorSet;
    sets[1] = bindlessDescriptor.descriptorSet;

    const auto gx = (width + 7)/8;
    const auto gy = (height + 7)/8;

    glm::vec2 dir{1, 0};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("sss_blur"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("sss_blur"), 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute.layout("sss_blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec2), &dir);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barrier::computeWriteToRead(commandBuffer);

    dir = {0, 1};
    vkCmdPushConstants(commandBuffer, compute.layout("sss_blur"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(glm::vec2), &dir);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void SubsurfaceScatteringDemo::initShadowMap() {
    textures::create(device, shadowMap.texture, VK_IMAGE_TYPE_2D, shadowMap.format, {shadowMap.size, shadowMap.size, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    shadowMap.renderInfo = Offscreen::RenderInfo{
            .depthAttachment = {{shadowMap.texture.imageView, shadowMap.format}},
            .renderArea = {shadowMap.size, shadowMap.size}
    };
    light.cpu->shadowMapIndex = bindlessDescriptor.update(shadowMap.texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void SubsurfaceScatteringDemo::captureShadow(VkCommandBuffer commandBuffer) {
    Barrier::fragmentReadToFragmentWrite(commandBuffer);
    offscreen.render(commandBuffer, shadowMap.renderInfo, [&]{
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.shadowMap.pipeline.handle);
        vkCmdPushConstants(commandBuffer, render.shadowMap.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &shadowMap.lightViewMatrix);
        model.draw(commandBuffer);
    });
    Barrier::fragmentWriteToFragmentRead(commandBuffer);
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
        settings.enabledFeatures.samplerAnisotropy = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = SubsurfaceScatteringDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}