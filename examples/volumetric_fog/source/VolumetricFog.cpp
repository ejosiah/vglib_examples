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

    static VkPhysicalDeviceSynchronization2Features sync2Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};
    sync2Features.synchronization2 = VK_TRUE;

    deviceCreateNextChain = &sync2Features;
    static VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
    dynamicRenderingFeatures.pNext = &sync2Features;
    dynamicRenderingFeatures.dynamicRendering = VK_TRUE;
    deviceCreateNextChain = &dynamicRenderingFeatures;
}

void VolumetricFog::initApp() {
    textures::color(device, dummyTexture, glm::vec3{0.2}, glm::uvec3{64});
    createBox();
    createHaltonSamples();
    initBindlessDescriptor();
    bakeVolumetricNoise();
    initLoader();
    bakeVolumetricNoise();
    createDescriptorPool();
    loadBlueNoise();
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

void VolumetricFog::createHaltonSamples() {
    static auto halton = [](int32_t i, int32_t b){
        float f = 1.0f;
        float r = 0.0f;
        while(i > 0) {
            f = f / static_cast<float>( b );
            r = r + f * static_cast<float>( i % b );
            i = i / b;
        }
        return r;
    };
    static auto halton2 = [&](int32_t i){ return halton(i, 2); };
    static auto halton3 = [&](int32_t i){ return halton(i, 3); };

    for(auto i = 0; i < JitterPeriod; ++i) {
        glm::vec2 sample{ halton2(i + 1), halton3(i + 1) };
        m_haltonSamples.push_back(sample);
    }
}

void VolumetricFog::initBindlessDescriptor() {
    m_bindLessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet(11);
    fogDataImageId = m_bindLessDescriptor.nextIndex(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    lightContribImageId[0] = m_bindLessDescriptor.nextIndex(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    lightContribImageId[1] = m_bindLessDescriptor.nextIndex(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    lightScatteringImageId = m_bindLessDescriptor.nextIndex(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    volumeNoiseImageId = m_bindLessDescriptor.nextIndex(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
}

void VolumetricFog::bakeVolumetricNoise() {
    textures::create(device, m_volumeNoise, VK_IMAGE_TYPE_3D, VK_FORMAT_R32_SFLOAT, {64, 64, 64}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    m_bindLessDescriptor.update({ &m_volumeNoise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9U });
    m_bindLessDescriptor.update({ &m_volumeNoise, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, volumeNoiseImageId, VK_IMAGE_LAYOUT_GENERAL });

    VkSpecializationMapEntry entry{0, 0, sizeof(uint32_t)};
    VkSpecializationInfo specializationInfo{1, &entry, sizeof(uint32_t), &volumeNoiseImageId};

    auto shader = device.createShaderModule(resource("volume_noise.comp.spv"));
    auto stage = initializers::shaderStage({ shader, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;
    auto layout = device.createPipelineLayout({ *m_bindLessDescriptor.descriptorSetLayout });

    auto info = initializers::computePipelineCreateInfo();
    info.stage = stage;
    info.layout = layout.handle;

    auto pipeline = device.createComputePipeline(info);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = m_volumeNoise.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout.handle, 0, 1, &m_bindLessDescriptor.descriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle);
        vkCmdDispatch(commandBuffer, 8, 8, 64);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    });

}

void VolumetricFog::initLoader() {
    m_loader = std::make_unique<asyncml::Loader>( &device, 32);
    m_loader->start();
}

void VolumetricFog::loadBlueNoise() {
    textures::fromFile(device, blueNoise, resource("LDR_RG01_0.png"), false, VK_FORMAT_R8G8B8A8_SRGB);
    m_bindLessDescriptor.update({ &blueNoise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8u});
}

void VolumetricFog::initSky() {
    sky = Sky{&fileManager, &device, &descriptorPool, &renderPass, {width, height}, &camera->camera, &m_scene, &m_bindLessDescriptor};
    sky.init();
}

void VolumetricFog::initFog() {
    m_fog.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(FogData), "fog_ubo");
    m_fog.cpu = reinterpret_cast<FogData*>(m_fog.gpu.map());

    m_fog.cpu->inverseVolumeTransform = glm::mat4{1};
    m_fog.cpu->froxelDim = glm::ivec3(128, 128, zSamples);
    m_fog.cpu->scatteringFactor = 0.1;
    m_fog.cpu->constantDensity = 0.1;
    m_fog.cpu->heightFogDensity = 0;
    m_fog.cpu->heightFogFalloff = 1.0;
    m_fog.cpu->g = 0;
    m_fog.cpu->volumeNoisePositionScale = 1.0;
    m_fog.cpu->volumeNoiseSpeedScale = 0.2;
    m_fog.cpu->applySpatialFiltering = 0;
    m_fog.cpu->applyTemporalFiltering = 0;
    m_fog.cpu->temporalFilterJitterScale = 1.6;
    m_fog.cpu->temporalFilterBlendWeight = 0.2;
    m_fog.cpu->jitter = glm::vec2(0);
}

void VolumetricFog::createFogTextures() {
    textures::create(device, m_fog.fogData, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, zSamples}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    textures::create(device, m_fog.lightContribution[0], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, zSamples}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    textures::create(device, m_fog.lightContribution[1], VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, zSamples}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));
    textures::create(device, m_fog.integratedScattering, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {128, 128, zSamples}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, sizeof(float));

    device.setName<VK_OBJECT_TYPE_IMAGE>("data_inject", m_fog.fogData.image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("data_inject_view", m_fog.fogData.imageView.handle);

    device.setName<VK_OBJECT_TYPE_IMAGE>("light_contrib_0", m_fog.lightContribution[0].image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("light_contrib_view_0", m_fog.lightContribution[0].imageView.handle);


    device.setName<VK_OBJECT_TYPE_IMAGE>("light_contrib_1", m_fog.lightContribution[1].image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("light_contrib_view_1", m_fog.lightContribution[1].imageView.handle);

    device.setName<VK_OBJECT_TYPE_IMAGE>("light_scattering", m_fog.integratedScattering.image.image);
    device.setName<VK_OBJECT_TYPE_IMAGE_VIEW>("light_scattering_view", m_fog.integratedScattering.imageView.handle);

    m_fog.fogData.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_fog.lightContribution[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    m_fog.lightContribution[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    m_bindLessDescriptor.update({ &m_fog.fogData, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, fogDataImageId, VK_IMAGE_LAYOUT_GENERAL});
    m_bindLessDescriptor.update({&m_fog.lightContribution[0], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, lightContribImageId[0], VK_IMAGE_LAYOUT_GENERAL});
    m_bindLessDescriptor.update({&m_fog.lightContribution[1], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, lightContribImageId[1], VK_IMAGE_LAYOUT_GENERAL});
    m_bindLessDescriptor.update({ &m_fog.integratedScattering, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, lightScatteringImageId, VK_IMAGE_LAYOUT_GENERAL});

    m_bindLessDescriptor.update({ &m_fog.fogData, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5U});
    m_bindLessDescriptor.update({ &m_fog.lightContribution[0], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6U});
    m_bindLessDescriptor.update({ &m_fog.integratedScattering, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7U});
}

void VolumetricFog::initShadowMap() {
    m_shadowMap = ShadowMap{ &fileManager, &device, &descriptorPool, &m_scene };
    m_shadowMap.init();
    m_bindLessDescriptor.update({ &m_shadowMap.texture(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1U});
}

void VolumetricFog::loadModel() {
    m_sponza = m_loader->load(R"(C:/Users/Josiah Ebhomenye/source/repos/VolumetricLighting/bin/Debug/meshes/sponza.obj)", centimetre);
//    m_smoke = m_loader->loadVolume(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\volumes\_VDB-Smoke-Pack\smoke_044_Low_Res\smoke_044_0.10_0106.vdb)");
    m_smoke = m_loader->loadVolume(R"(C:\Users\Josiah Ebhomenye\OneDrive\media\volumes\VDB-Clouds-Pack-Pixel-Lab\VDB Cloud Files\cloud_v001_0.02.vdb)");
}

void VolumetricFog::initScene() {
    constexpr float SunAngularRadius = 0.004675;
    m_scene.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SceneData), "scene");
    m_scene.cpu = reinterpret_cast<SceneData*>(m_scene.gpu.map());
    m_scene.cpu->earthCenter = {0, - 6360 * km, 0};
    m_scene.cpu->sunSize = {glm::tan(SunAngularRadius), glm::cos(SunAngularRadius), 0};
    m_scene.cpu->cameraDirection = glm::vec3(0, -1, 0);
    m_scene.cpu->cameraLightOn = 0;
    m_scene.cpu->cameraLightCut0ff = glm::cos(glm::radians(10.f));
    m_scene.cpu->sunDirection = glm::normalize(glm::vec3(1));
    m_scene.cpu->whitePoint = glm::vec3(1);
    m_scene.cpu->exposure = 10.f;
    m_scene.cpu->znear = camera->znear;
    m_scene.cpu->zfar = camera->zfar;
    m_scene.cpu->viewProjection = camera->camera.proj * camera->camera.view * camera->camera.model;
    m_scene.cpu->inverseViewProjection = glm::inverse(m_scene.cpu->viewProjection);
    m_scene.cpu->previousViewProjection = m_scene.cpu->viewProjection;
    m_scene.cpu->previousInverseViewProjection = m_scene.cpu->inverseViewProjection;
    m_scene.cpu->frame = 0;
    m_scene.camera = &camera->camera;
    m_scene.zNear = camera->znear;
    m_scene.zFar = camera->zfar;
    m_scene.cpu->screenWidth = swapChain.width();
    m_scene.cpu->screenHeight = swapChain.height();
    m_scene.bounds =
            std::make_tuple(glm::vec3{-19.2094593048, -1.2644249201, -11.8280706406}
                            , glm::vec3{17.9990806580, 14.2943315506, 11.0542602539});

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
//    auto position = glm::vec3(-0.6051893234, 6.5149531364, -0.3869051933);
    auto position = glm::vec3(2.8, 1.9, -0.6);
    spdlog::info("camera position: {}", position);
    camera->position(position);
}


void VolumetricFog::createDescriptorPool() {
    constexpr uint32_t maxSets = 500;
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
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    m_fog.uboDescriptorSetLayout =
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
        .createLayout();

    m_meshSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("mesh_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(m_sponza->numMeshes())
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(m_sponza->numMaterials())
                .shaderStages(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
            .createLayout();

    auto sets = descriptorPool.allocate({ m_scene.sceneSetLayout, m_fog.uboDescriptorSetLayout, m_meshSetLayout });

    m_scene.sceneSet = sets[0];
    m_fog.uboDescriptorSet = sets[1];
    m_meshDescriptorSet= sets[2];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("scene_descriptor_set", m_scene.sceneSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fog_ubo_descriptor_set", m_fog.uboDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("mesh_descriptor_set", m_meshDescriptorSet);

}

void VolumetricFog::updateFogDescriptorSets() {
    auto writes = initializers::writeDescriptorSets<2>();
    
    writes[0].dstSet = m_fog.uboDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    VkDescriptorBufferInfo uboInfo{ m_fog.gpu, 0, VK_WHOLE_SIZE};
    writes[0].pBufferInfo = &uboInfo;

    writes[1].dstSet = m_fog.uboDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo blueNoiseInfo{ blueNoise.sampler.handle, blueNoise.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &blueNoiseInfo;


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
    updateMeshDescriptorSet();
}

void VolumetricFog::updateMeshDescriptorSet() {
    auto writes = initializers::writeDescriptorSets();
    writes.resize(BindLessDescriptorPlugin::MaxDescriptorResources);

//    VkDescriptorImageInfo dummyImageInfo{ dummyTexture.sampler.handle, dummyTexture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
//    for(auto i = 0; i < writes.size(); i++) {
//        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//        writes[i].dstSet = m_bindLessDescriptor.descriptorSet;
//        writes[i].dstBinding = BindLessDescriptorPlugin::TextureResourceBindingPoint;
//        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//        writes[i].dstArrayElement = i;
//        writes[i].descriptorCount = 1;
//        writes[i].pImageInfo = &dummyImageInfo;
//    }
//
//    device.updateDescriptorSets(writes);

    const auto numMeshes = m_sponza->numMeshes();
    const VkDeviceSize meshDataSize = sizeof(asyncml::MeshData);
    std::vector<VkDescriptorBufferInfo> meshInfos;
    for(auto i = 0; i < numMeshes; i++) {
        const VkDeviceSize offset = i * meshDataSize;
        meshInfos.push_back({m_sponza->meshBuffer, offset, meshDataSize  });
    }

    writes.resize(2);
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_meshDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorCount = numMeshes;
    writes[0].pBufferInfo = meshInfos.data();

    const auto numMaterials = m_sponza->numMaterials();
    const VkDeviceSize materialDataSize = sizeof(asyncml::MaterialData);
    std::vector<VkDescriptorBufferInfo> materialInfos;
    for(auto i = 0; i < numMaterials; i++){
        materialInfos.push_back({m_sponza->materialBuffer, i * materialDataSize, materialDataSize});
    }
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_meshDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorCount = numMaterials;
    writes[1].pBufferInfo = materialInfos.data();

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
                    .addDescriptorSetLayout(m_meshSetLayout)
                    .addDescriptorSetLayout(m_scene.sceneSetLayout)
                    .addDescriptorSetLayout(m_shadowMap.lightMatrixDescriptorSetLayout)
                    .addDescriptorSetLayout(m_shadowMap.shadowMapDescriptorSetLayout)
                    .addDescriptorSetLayout(*m_bindLessDescriptor.descriptorSetLayout)
                    .addDescriptorSetLayout(m_fog.uboDescriptorSetLayout)
                    .name("render")
                .build(render.layout);
    //    @formatter:off
        rayMarch.pipeline =
            builder
                .basePipeline(render.pipeline)
                .shaderStage()
                    .vertexShader(resource("render.vert.spv"))
                    .fragmentShader(resource("ray_march.frag.spv"))
                .name("render_ray_march")
            .build(rayMarch.layout);
    //    @formatter:on

    volumeOutline.pipeline =
            prototypes->cloneGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("flat.vert.spv"))
                    .fragmentShader(resource("flat.frag.spv"))
                .inputAssemblyState()
                    .lines()
                .name("volume_outline")
            .build(volumeOutline.layout);
}

void VolumetricFog::createComputePipeline() {
    std::vector<uint32_t> constantIds{ fogDataImageId, lightContribImageId[0], lightContribImageId[1], lightScatteringImageId  };
    std::vector<VkSpecializationMapEntry> entries{
            { 0, 0, sizeof(uint32_t)},
            { 1, sizeof(uint32_t), sizeof(uint32_t)},
            { 2, sizeof(uint32_t) * 2, sizeof(uint32_t)},
            { 3, sizeof(uint32_t) * 3, sizeof(uint32_t)},
    };

    VkSpecializationInfo specializationInfo{ COUNT(entries), entries.data(), BYTE_SIZE(constantIds), constantIds.data() };

    dataInjection.layout = device.createPipelineLayout({
        m_fog.uboDescriptorSetLayout,
        m_scene.sceneSetLayout,
        *m_bindLessDescriptor.descriptorSetLayout});

    auto module = device.createShaderModule(resource("data_injection.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    stage.pSpecializationInfo = &specializationInfo;
    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = dataInjection.layout.handle;

    dataInjection.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    lightContrib.layout = device.createPipelineLayout({
                  m_fog.uboDescriptorSetLayout,
                  m_scene.sceneSetLayout,
                  sky.descriptor().uboDescriptorSetLayout,
                  sky.descriptor().lutDescriptorSetLayout,
                  m_shadowMap.shadowMapDescriptorSetLayout,
                  *m_bindLessDescriptor.descriptorSetLayout});
    module = device.createShaderModule(resource("light_contrib.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = lightContrib.layout.handle;

    lightContrib.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    lightIntegration.layout = device.createPipelineLayout({
                                  m_fog.uboDescriptorSetLayout,
                                  m_scene.sceneSetLayout,
                                  sky.descriptor().uboDescriptorSetLayout,
                                  sky.descriptor().lutDescriptorSetLayout,
                                  *m_bindLessDescriptor.descriptorSetLayout});
    module = device.createShaderModule(resource("integrate.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = lightIntegration.layout.handle;
    lightIntegration.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);

    module = device.createShaderModule(resource("spatial_filter.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT });
    spatialFilter.layout = device.createPipelineLayout({ m_fog.uboDescriptorSetLayout, *m_bindLessDescriptor.descriptorSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = spatialFilter.layout.handle;
    spatialFilter.pipeline = device.createComputePipeline(computeCreateInfo);

    module = device.createShaderModule(resource("temporal_filter.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT });
    temporalFilter.layout = device.createPipelineLayout({ m_fog.uboDescriptorSetLayout, m_scene.sceneSetLayout, *m_bindLessDescriptor.descriptorSetLayout });
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = temporalFilter.layout.handle;
    temporalFilter.pipeline = device.createComputePipeline(computeCreateInfo);

    device.setName<VK_OBJECT_TYPE_PIPELINE>("data_injection", dataInjection.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("light_contrib", lightContrib.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("spatial_filter", spatialFilter.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("temporal_filter", temporalFilter.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("integrate", lightIntegration.pipeline.handle);
}


void VolumetricFog::onSwapChainDispose() {
    dispose(render.pipeline);
}

void VolumetricFog::onSwapChainRecreation() {
    camera->perspective(swapChain.aspectRatio());
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
    sky.reload(&renderPass, width, height);
    m_scene.cpu->screenWidth = swapChain.width();
    m_scene.cpu->screenHeight = swapChain.height();
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
    if(raymarch) {
        renderWithRayMarching(commandBuffer);
    }else {
        renderWithVolumeTextures(commandBuffer);
    }
    renderVolumeOutline(commandBuffer);
    renderUI(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumetricFog::renderWithVolumeTextures(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 8> sets;
    sets[0] = sky.descriptor().uboDescriptorSet;
    sets[1] = sky.descriptor().lutDescriptorSet;
    sets[2] = m_meshDescriptorSet;
    sets[3] = m_scene.sceneSet;
    sets[4] = m_shadowMap.lightMatrixDescriptorSet;
    sets[5] = m_shadowMap.shadowMapDescriptorSet;
    sets[6] = m_bindLessDescriptor.descriptorSet;
    sets[7] = m_fog.uboDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, render.layout);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_sponza->vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_sponza->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_sponza->draw.gpu, 0, m_sponza->draw.count, sizeof(VkDrawIndexedIndirectCommand));
}

void VolumetricFog::renderWithRayMarching(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 8> sets;
    sets[0] = sky.descriptor().uboDescriptorSet;
    sets[1] = sky.descriptor().lutDescriptorSet;
    sets[2] = m_meshDescriptorSet;
    sets[3] = m_scene.sceneSet;
    sets[4] = m_shadowMap.lightMatrixDescriptorSet;
    sets[5] = m_shadowMap.shadowMapDescriptorSet;
    sets[6] = m_bindLessDescriptor.descriptorSet;
    sets[7] = m_fog.uboDescriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rayMarch.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    camera->push(commandBuffer, rayMarch.layout);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_sponza->vertexBuffer, &offset);
    vkCmdBindIndexBuffer(commandBuffer, m_sponza->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer, m_sponza->draw.gpu, 0, m_sponza->draw.count, sizeof(VkDrawIndexedIndirectCommand));
}

void VolumetricFog::renderVolumeOutline(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, volumeOutline.pipeline.handle);
    camera->push(commandBuffer, volumeOutline.layout, m_smoke->transform);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, boxBuffer, &offset);
    vkCmdDraw(commandBuffer, 24, 1, 0, 0);
    camera->setModel(glm::mat4{1});
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
        ImGui::SliderFloat("volume position scale", &m_fog.cpu->volumeNoisePositionScale, 0, 1);
        ImGui::SliderFloat("volume speed scale", &m_fog.cpu->volumeNoiseSpeedScale, 0, 1);
        ImGui::Checkbox("raymarch", &raymarch);

        static bool applySpatialFiltering = bool(m_fog.cpu->applySpatialFiltering);
        ImGui::Checkbox("Spatial filtering", &applySpatialFiltering);
        m_fog.cpu->applySpatialFiltering = int(applySpatialFiltering);

        ImGui::SameLine();
        static bool applyTemporalFiltering = bool(m_fog.cpu->applyTemporalFiltering);
        ImGui::Checkbox("Temporal filtering", &applyTemporalFiltering);
        m_fog.cpu->applyTemporalFiltering = int(applyTemporalFiltering);

        if(applyTemporalFiltering) {
            ImGui::SliderFloat("Temporal jitter scale", &m_fog.cpu->temporalFilterJitterScale, 0, 10);
            ImGui::SliderFloat("Temporal blend weight", &m_fog.cpu->temporalFilterBlendWeight, 0, 1);
        }

        if(ImGui::CollapsingHeader("volume")){
            static bool dirty = false;
            dirty |= ImGui::SliderFloat("scale", &m_smoke->scale, 0.01, 1.0);
            dirty |= ImGui::SliderFloat("x", &m_smoke->offset.x, -50, 50);
            dirty |= ImGui::SliderFloat("y", &m_smoke->offset.y, -50, 50);
            dirty |= ImGui::SliderFloat("z", &m_smoke->offset.z, -50, 50);
            if(dirty) {
                m_smoke->updateTransform();
                dirty = false;
            }
        }
    }

    if(ImGui::CollapsingHeader("Camera")){
        static bool lightOn = m_scene.cpu->cameraLightOn;
        ImGui::Checkbox("camera light", &lightOn);
        m_scene.cpu->cameraLightOn = int(lightOn);
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
        m_scene.cpu->camera = camera->position();
        m_scene.cpu->cameraDirection = camera->viewDir;
    }

    m_scene.cpu->time += time;
    m_scene.cpu->deltaTime = time;
    updateSunDirection();
    sky.update(time);
    castShadow();
    m_scene.cpu->lightViewProjection = m_shadowMap.lightSpace.cpu->viewProj;
    m_smoke->update(time);

    computeFog();

    auto wTitle = fmt::format("{} - camera: {:f}", title, camera->position());
    glfwSetWindowTitle(window, wTitle.c_str());
}

void VolumetricFog::checkAppInputs() {
    camera->processInput();
}

void VolumetricFog::cleanup() {
    m_loader->stop();
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
}

void VolumetricFog::castShadow() {
    m_shadowMap.generate([this](auto commandBuffer){
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, m_sponza->vertexBuffer, &offset);
        vkCmdBindIndexBuffer(commandBuffer, m_sponza->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer, m_sponza->draw.gpu, 0, m_sponza->draw.count, sizeof(VkDrawIndexedIndirectCommand));
    });
}

void VolumetricFog::computeFog() {
    m_fog.cpu->inverseVolumeTransform = glm::inverse(m_smoke->transform);
    m_fog.cpu->boxMin = m_smoke->bounds.min * m_smoke->scale;
    m_fog.cpu->boxMax = m_smoke->bounds.max * m_smoke->scale;

    static bool once = false;
    if(!once && m_smoke->initialized) {
        once = true;
        auto bounds = m_smoke->scaledBounds();
        auto wBounds = m_smoke->scaledBounds();
        auto center = (wBounds.max + wBounds.min) * .5f;
        bounds.min = (m_fog.cpu->inverseVolumeTransform * glm::vec4(bounds.min, 1));
        bounds.max = (m_fog.cpu->inverseVolumeTransform * glm::vec4(bounds.max, 1));
        auto lcenter = (m_fog.cpu->inverseVolumeTransform * glm::vec4(center, 1));
        spdlog::error("worldSpace bounds: [{}, {}], localSpace bounds: [{}, {}]", wBounds.min, wBounds.max, bounds.min, bounds.max);
        spdlog::error("worldSpace center: {}, localSpace center: {}", center, lcenter);
    }

    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        // DATA INJECTION
        static std::vector<VkDescriptorSet> sets;
        sets.resize(3);
        sets[0] = m_fog.uboDescriptorSet;
        sets[1] = m_scene.sceneSet;
        sets[2] = m_bindLessDescriptor.descriptorSet;


        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = m_fog.fogData.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dataInjection.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, dataInjection.pipeline.handle);
        vkCmdDispatch(commandBuffer, 16, 16, zSamples);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        // LIGHT SCATTERING
        sets.resize(6);
        sets[0] = m_fog.uboDescriptorSet;
        sets[1] = m_scene.sceneSet;
        sets[2] = sky.descriptor().uboDescriptorSet;
        sets[3] = sky.descriptor().lutDescriptorSet;
        sets[4] = m_shadowMap.shadowMapDescriptorSet;
        sets[5] = m_bindLessDescriptor.descriptorSet;

        int lightScatteringIndex = m_scene.cpu->frame % 2;

        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.image = m_fog.lightContribution[lightScatteringIndex].image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lightContrib.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lightContrib.pipeline.handle);
        vkCmdDispatch(commandBuffer, 16, 16, zSamples);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        
        // SPATIAL FILTERING
        sets.resize(2);
        sets[0] = m_fog.uboDescriptorSet;
        sets[1] = m_bindLessDescriptor.descriptorSet;

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, spatialFilter.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, spatialFilter.pipeline.handle);
        vkCmdDispatch(commandBuffer, 16, 16, zSamples);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.image = m_fog.fogData.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        // TEMPORAL FILTERING
        sets.resize(3);
        sets[0] = m_fog.uboDescriptorSet;
        sets[1] = m_scene.sceneSet;
        sets[2] = m_bindLessDescriptor.descriptorSet;
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, temporalFilter.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, temporalFilter.pipeline.handle);
        vkCmdDispatch(commandBuffer, 16, 16, zSamples);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.image = m_fog.lightContribution[lightScatteringIndex].image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);


        // LIGHT INTEGRATION
        sets.resize(5);
        sets[0] = m_fog.uboDescriptorSet;
        sets[1] = m_scene.sceneSet;
        sets[2] = sky.descriptor().uboDescriptorSet;
        sets[3] = sky.descriptor().lutDescriptorSet;
        sets[4] = m_bindLessDescriptor.descriptorSet;

        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = m_fog.integratedScattering.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lightIntegration.layout.handle, 0, sets.size(), sets.data(), 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, lightIntegration.pipeline.handle);
        vkCmdDispatch(commandBuffer, 16, 16, 1);

        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.image = m_fog.integratedScattering.image;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

}

void VolumetricFog::newFrame() {
    m_fog.cpu->jitter = 2.f * m_haltonSamples[m_jitterIndex] - 1.0f;
    ++m_jitterIndex;
    m_jitterIndex %= JitterPeriod;
}

void VolumetricFog::endFrame() {
    m_sponza->updateDrawState(device, m_bindLessDescriptor);
    m_scene.cpu->frame++;
    m_scene.cpu->previousViewProjection = m_scene.cpu->viewProjection;
    m_scene.cpu->previousInverseViewProjection = m_scene.cpu->inverseViewProjection;

    m_smoke->checkLoadState(m_bindLessDescriptor);
    m_smoke->advanceFrame(device);
}


void VolumetricFog::createBox() {
    const glm::vec4 color{1, 1, 0, 1};
    std::vector<Vertex> vertices {
            // FRONT
            { .position = glm::vec4(0, 0, 0, 1), .color = color},
            { .position = glm::vec4(0, 1, 0, 1), .color = color},

            { .position = glm::vec4(0, 1, 0, 1), .color = color},
            { .position = glm::vec4(1, 1, 0, 1), .color = color},

            { .position = glm::vec4(1, 1, 0, 1), .color = color},
            { .position = glm::vec4(1, 0, 0, 1), .color = color},

            { .position = glm::vec4(1, 0, 0, 1), .color = color},
            { .position = glm::vec4(0, 0, 0, 1), .color = color},

            // BACK
            { .position = glm::vec4(0, 0, 1, 1), .color = color},
            { .position = glm::vec4(0, 1, 1, 1), .color = color},

            { .position = glm::vec4(0, 1, 1, 1), .color = color},
            { .position = glm::vec4(1, 1, 1, 1), .color = color},

            { .position = glm::vec4(1, 1, 1, 1), .color = color},
            { .position = glm::vec4(1, 0, 1, 1), .color = color},

            { .position = glm::vec4(1, 0, 1, 1), .color = color},
            { .position = glm::vec4(0, 0, 1, 1), .color = color},


            // SIDES
            { .position = glm::vec4(0, 0, 0, 1), .color = color},
            { .position = glm::vec4(0, 0, 1, 1), .color = color},

            { .position = glm::vec4(0, 1, 0, 1), .color = color},
            { .position = glm::vec4(0, 1, 1, 1), .color = color},

            { .position = glm::vec4(1, 1, 0, 1), .color = color},
            { .position = glm::vec4(1, 1, 1, 1), .color = color},

            { .position = glm::vec4(1, 0, 0, 1), .color = color},
            { .position = glm::vec4(1, 0, 1, 1), .color = color},


    };

    boxBuffer = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}


int main(){
    try{

        Settings settings;
        settings.width = 1920;
        settings.height = 1080;
        settings.depthTest = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        auto app = VolumetricFog{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}