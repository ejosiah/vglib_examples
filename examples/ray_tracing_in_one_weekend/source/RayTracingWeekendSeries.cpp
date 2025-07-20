#include "RayTracingWeekendSeries.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "implicit_shapes.hpp"

RayTracingWeekendSeries::RayTracingWeekendSeries(const Settings& settings) : VulkanRayTraceBaseApp("Ray tracing in one weekend", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("ray_tracing_in_one_weekend");
    fileManager().addSearchPathFront("ray_tracing_in_one_weekend/data");
    fileManager().addSearchPathFront("ray_tracing_in_one_weekend/spv");
    fileManager().addSearchPathFront("ray_tracing_in_one_weekend/models");
    fileManager().addSearchPathFront("ray_tracing_in_one_weekend/textures");

    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;
    info.vertexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialBufferMemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;
}

void RayTracingWeekendSeries::initApp() {
    initCamera();
    initCanvas();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createCheckerboardTexture();
    loadTextures();
    initLoader();
    initUniforms();
    loadMeshes();
    loadDefaultScene();
    loadInOneWeekendScene();
    loadInOneWeekendTrianglesScene();
    loadTextureScene();
    loadPerlinNoiseScene();
    loadLightScene();
    loadCornellBoxScene();
    loadCornellBoxVolumeScene();
    loadTheNextWeekEndScene();
    loadForTheRestOfYourLife();
    loadScene();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createComputePipelines();
    computePerlinNoise();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createRayTracingPipeline();
    sceneLabels = map_range(scenes, [](const auto& scene){ return scene.name.c_str(); });
}

void RayTracingWeekendSeries::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 45.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->zoomDelta = 5;
}

void RayTracingWeekendSeries::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
}

void RayTracingWeekendSeries::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto posFetchFeature = findExtension<VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR, deviceCreateNextChain);
    posFetchFeature->rayTracingPositionFetch = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void RayTracingWeekendSeries::createDescriptorPool() {
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


void RayTracingWeekendSeries::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void RayTracingWeekendSeries::createDescriptorSetLayouts() {
    raytrace.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ray_trace")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4) // noise
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    adaptiveSampling.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("adaptive_sampling")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void RayTracingWeekendSeries::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { raytrace.descriptorSetLayout, adaptiveSampling.descriptorSetLayout });
    raytrace.descriptorSet = sets[0];
    adaptiveSampling.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<10>();

    VkWriteDescriptorSetAccelerationStructureKHR asWrites{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
    asWrites.accelerationStructureCount = 1;
    asWrites.pAccelerationStructures =  rtBuilder.accelerationStructure();
    writes[0].pNext = &asWrites;
    writes[0].dstSet = raytrace.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[0].descriptorCount = 1;

    writes[1].dstSet = raytrace.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    VkDescriptorBufferInfo camInfo{ uniforms.gpu, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &camInfo;

    writes[2].dstSet = raytrace.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, rtxOutput.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;

    writes[3].dstSet = raytrace.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo convergeInfo{ VK_NULL_HANDLE, converged.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &convergeInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = raytrace.descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo noiseInfo{ noise.sampler.handle, noise.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[4].pImageInfo = &noiseInfo;

    writes[5].dstSet = adaptiveSampling.descriptorSet;
    writes[5].dstBinding = 0;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo sourceInfo{ VK_NULL_HANDLE, rtxOutput.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[5].pImageInfo = &sourceInfo;

    writes[6].dstSet = adaptiveSampling.descriptorSet;
    writes[6].dstBinding = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo meanInfo{ VK_NULL_HANDLE, canvas.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[6].pImageInfo = &meanInfo;

    writes[7].dstSet = adaptiveSampling.descriptorSet;
    writes[7].dstBinding = 2;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = 1;
    VkDescriptorImageInfo M2Info{ VK_NULL_HANDLE, meanSquared.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[7].pImageInfo = &M2Info;

    writes[8].dstSet = adaptiveSampling.descriptorSet;
    writes[8].dstBinding = 3;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[8].descriptorCount = 1;
    VkDescriptorImageInfo errorInfo{ VK_NULL_HANDLE, stdError.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[8].pImageInfo = &errorInfo;

    writes[9].dstSet = adaptiveSampling.descriptorSet;
    writes[9].dstBinding = 4;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[9].descriptorCount = 1;
    writes[9].pImageInfo = &convergeInfo;

    device.updateDescriptorSets(writes);
}

void RayTracingWeekendSeries::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void RayTracingWeekendSeries::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void RayTracingWeekendSeries::initCanvas() {
    const auto imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    canvas = Canvas{ this, imageUsage, VK_FORMAT_R32G32B32A32_SFLOAT, {}, resource("display.frag.spv"),
                     VkPushConstantRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapping)}};
    canvas.init();
    canvas.setConstants(&toneMapping);

    textures::create(device, rtxOutput, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    rtxOutput.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::create(device, meanSquared, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    meanSquared.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::create(device, stdError, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    stdError.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    textures::create(device, converged, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, {width, height, 1});
    converged.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    std_errTexId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(stdError, VK_IMAGE_LAYOUT_GENERAL);

}

void RayTracingWeekendSeries::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));
    auto missShaderModule = device.createShaderModule( resource("main.rmiss.spv"));
    auto diffuseHitShaderModule = device.createShaderModule( resource("diffuse_imp.rchit.spv"));
    auto metalHitShaderModule = device.createShaderModule( resource("metal_imp.rchit.spv"));
    auto dielectricHitShaderModule = device.createShaderModule( resource("dielectric_imp.rchit.spv"));
    auto implicitsIntersectShaderModule = device.createShaderModule( resource("main.rint.spv"));

//    auto diffuseTriHitShaderModule = device.createShaderModule( resource("normal.rchit.spv"));
    auto diffuseTriHitShaderModule = device.createShaderModule( resource("diffuse_tri.rchit.spv"));
    auto metalTriHitShaderModule = device.createShaderModule( resource("metal_tri.rchit.spv"));
    auto dielectricTriHitShaderModule = device.createShaderModule( resource("dielectric_tri.rchit.spv"));

    auto volumeHitShaderModule = device.createShaderModule( resource("volume.rchit.spv"));

    auto shaders = std::vector<ShaderInfo>(to<int>(ShaderIndex::Count));
    shaders[to<int>(ShaderIndex::RayGen)] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[to<int>(ShaderIndex::Miss)] = { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};

    shaders[to<int>(ShaderIndex::DiffuseHitImpl)] = { diffuseHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::MetalHitImpl)] = { metalHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::DielectricHitImpl)] = { dielectricHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::ImplIntersect)] = {implicitsIntersectShaderModule, VK_SHADER_STAGE_INTERSECTION_BIT_KHR};

    shaders[to<int>(ShaderIndex::DiffuseHitTri)] = { diffuseTriHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::MetalHitTri)] = { metalTriHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::DielectricHitTri)] = { dielectricTriHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    shaders[to<int>(ShaderIndex::VolumeHit)] = { volumeHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};


    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup());

    shaderGroups.push_back(shaderTablesDesc.addMissGroup(to<int>(ShaderIndex::Miss)));

    const auto& scene = scenes[currentScene];
    if(diffuseSpheres.size != 0) {
        const auto hitGroup = scene.implicits.diffuse.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DiffuseHitImpl), to<int>(ShaderIndex::ImplIntersect), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(diffuseSpheres));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(materials.diffuse));
    }

    if(metalSpheres.size != 0) {
        const auto hitGroup = scene.implicits.metal.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::MetalHitImpl), to<int>(ShaderIndex::ImplIntersect), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(metalSpheres));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(materials.metal));
    }

    if(dielectricSpheres != 0) {
        const auto hitGroup = scene.implicits.dielectric.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DielectricHitImpl), to<int>(ShaderIndex::ImplIntersect), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(dielectricSpheres));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(materials.dielectric));
    }

    if(triangleMaterials.diffuse.size != 0) {
        const auto hitGroup = scene.triangles.diffuse.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DiffuseHitTri)));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(triangleMaterials.diffuse));
    }

    if(triangleMaterials.metal.size != 0) {
        const auto hitGroup = scene.triangles.metal.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::MetalHitTri)));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(triangleMaterials.metal));
    }

    if(triangleMaterials.dielectric.size != 0) {
        const auto hitGroup = scene.triangles.dielectric.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DielectricHitTri)));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(triangleMaterials.dielectric));
    }

    if(mediums.size != 0) {
        const auto hitGroup = scene.triangles.volume.hitGroup;
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::VolumeHit)));
        shaderTablesDesc.hitGroups[hitGroup].addRecord(device.getAddress(mediums));
    }

    dispose(raytrace.layout);

    auto stages = map_range(shaders, [](auto& shader){
        return VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = shader.stage,
                .module = shader.module.handle,
                .pName = shader.entry,
        };
    });

    raytrace.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout, *bindlessDescriptor.descriptorSetLayout });
    VkRayTracingPipelineCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = COUNT(stages);
    createInfo.pStages = stages.data();
    createInfo.groupCount = COUNT(shaderGroups);
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 1;
    createInfo.layout = raytrace.layout.handle;

    raytrace.pipeline = device.createRayTracingPipeline(createInfo);
    bindingTables = shaderTablesDesc.compile(device, raytrace.pipeline);
}

void RayTracingWeekendSeries::rayTrace(VkCommandBuffer commandBuffer) {
    CanvasToRayTraceBarrier(commandBuffer);

    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = raytrace.descriptorSet;
    sets[1] = bindlessDescriptor.descriptorSet;
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);


    accumulateAdaptive(commandBuffer);
}

void RayTracingWeekendSeries::rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const {
    VkImageMemoryBarrier barrier = initializers::ImageMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = canvas.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;


    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0,

                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         1,
                         &barrier);
}

void RayTracingWeekendSeries::CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const {
    VkImageMemoryBarrier barrier = initializers::ImageMemoryBarrier();
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = canvas.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    VkMemoryBarrier mBarrier{};
    mBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    mBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    mBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0,
                         0,
                         VK_NULL_HANDLE,
                         0,
                         VK_NULL_HANDLE,
                         1,
                         &barrier);
}


void RayTracingWeekendSeries::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("pass_through.vert.spv"))
                    .fragmentShader(resource("pass_through.frag.spv"))
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void RayTracingWeekendSeries::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(raytrace.pipeline);
    clearAccelerationStructure();
    dispose(diffuseSpheres);
    dispose(metalSpheres);
    dispose(dielectricSpheres);
    dispose(mediums);
    dispose(triangleMaterials.dielectric);
    dispose(triangleMaterials.metal);
    dispose(triangleMaterials.diffuse);
    dispose(materials.dielectric);
    dispose(materials.metal);
    dispose(materials.diffuse);
    nextInstance = 0;
    uniforms.cpu->litBackGround = 1;
    uniforms.cpu->currentSample = 0;
}

void RayTracingWeekendSeries::onSwapChainRecreation() {
    initCanvas();
    loadScene();
    createRayTracingPipeline();
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *RayTracingWeekendSeries::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        canvas.draw(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void RayTracingWeekendSeries::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Settings");
    ImGui::SetWindowSize({0, 0});

    static int samples = to<int>(uniforms.cpu->sampleCount);
    static int bounces = to<int>(uniforms.cpu->maxBounce);
    static bool adaptive = uniforms.cpu->adaptiveSampling;
    static bool blueNoise = uniforms.cpu->blueNoise;
    static float apertureSize = uniforms.cpu->apertureSize;
    static float focalDistance = uniforms.cpu->focalDistance;
    static bool dirty = false;

    if(ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        sceneUpdated = ImGui::Combo("Scene##1", &currentScene, sceneLabels.data(), sceneLabels.size());
    }

    if(ImGui::CollapsingHeader("sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
        if(uniforms.cpu->adaptiveSampling == 0) {
            samples = std::clamp(samples, 1, 64);
            dirty |= ImGui::SliderInt("sample count", &samples, 1, 64);
        }else {
            samples = std::clamp(samples, 10000, 1000000);
            dirty |= ImGui::SliderInt("sample count", &samples, 1000, 1000000);
        }
        dirty |= ImGui::SliderInt("bounces", &bounces, 1, 50);
        dirty |= ImGui::Checkbox("adaptive sampling", &adaptive);
        dirty |= ImGui::Checkbox("use blue noise", &blueNoise);
    }

    if(ImGui::CollapsingHeader("camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        dirty |= ImGui::SliderFloat("aperture", &apertureSize, 0, 20);
        const auto maxDistance = glm::distance(camera->position(), camera->target);
        dirty |= ImGui::SliderFloat("focal distance", &focalDistance, 1, maxDistance);
    }

    if(ImGui::CollapsingHeader("convergence", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Image(std_errTexId, {128, 128});
        ImGui::SliderFloat("error threshold", &adaptiveSampling.constants.error_threshold, 0.1, 0.0);
    }

    if(ImGui::CollapsingHeader("ToneMapping", ImGuiTreeNodeFlags_DefaultOpen)) {
        static std::array<const char*, to<int>(ToneMappers::Count)> labels{ "Clamp", "Reinhard", "Uncharted 2", "ACES", "Hejl-Burgess-Dawson" };
        ImGui::Combo("Tone mapper", &toneMapping.method, labels.data(), labels.size());
        ImGui::SliderFloat("Exposure Value", &toneMapping.exposureValue, -3, 3);
    }

    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);

    uniforms.cpu->sampleCount = samples;
    uniforms.cpu->maxBounce = bounces;
    uniforms.cpu->adaptiveSampling = adaptive;
    uniforms.cpu->blueNoise = blueNoise;
    uniforms.cpu->apertureSize = apertureSize;
    uniforms.cpu->focalDistance = focalDistance;
    if(dirty) {
        uniforms.cpu->currentSample = 0;
        dirty = false;
    }
}

void RayTracingWeekendSeries::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }

    auto samples = uniforms.cpu->adaptiveSampling == 1 ? uniforms.cpu->currentSample : uniforms.cpu->sampleCount;
    setTitle(fmt::format("{}, FPS - {}, samples - {}", title, framePerSecond, samples));
}

void RayTracingWeekendSeries::checkAppInputs() {
    camera->processInput();
}

void RayTracingWeekendSeries::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void RayTracingWeekendSeries::onPause() {
    VulkanBaseApp::onPause();
}

void RayTracingWeekendSeries::loadScene() {
    assert(scenes.size() > currentScene);

    constexpr auto usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto& scene = scenes[currentScene];

    if(!scene.implicits.diffuse.spheres.empty()) {
        const auto hitGroup = nextHitGroup();
        diffuseSpheres = device.createDeviceLocalBuffer(scene.implicits.diffuse.spheres.data(), BYTE_SIZE(scene.implicits.diffuse.spheres), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("diffuse_spheres", diffuseSpheres.buffer);
        rtBuilder.add(scene.implicits.diffuse.spheres, 0, hitGroup * to<int>(RayType::Count));
        scene.implicits.diffuse.hitGroup = hitGroup;

        materials.diffuse = device.createDeviceLocalBuffer(scene.implicits.diffuse.materials.data(), BYTE_SIZE(scene.implicits.diffuse.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("diffuse_materials", materials.diffuse.buffer);
    }
    if(!scene.implicits.metal.spheres.empty()) {
        const auto hitGroup = nextHitGroup();
        metalSpheres = device.createDeviceLocalBuffer(scene.implicits.metal.spheres.data(), BYTE_SIZE(scene.implicits.metal.spheres), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("metal_spheres", metalSpheres.buffer);
        rtBuilder.add(scene.implicits.metal.spheres, 0, hitGroup * to<int>(RayType::Count));
        scene.implicits.metal.hitGroup = hitGroup;

        materials.metal = device.createDeviceLocalBuffer(scene.implicits.metal.materials.data(), BYTE_SIZE(scene.implicits.metal.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("metal_materials", materials.metal.buffer);
    }
    if(!scene.implicits.dielectric.spheres.empty()) {
        const auto hitGroup = nextHitGroup();
        dielectricSpheres = device.createDeviceLocalBuffer(scene.implicits.dielectric.spheres.data(), BYTE_SIZE(scene.implicits.dielectric.spheres), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("dielectric_spheres", dielectricSpheres.buffer);
        rtBuilder.add(scene.implicits.dielectric.spheres, 0, hitGroup * to<int>(RayType::Count));
        scene.implicits.dielectric.hitGroup = hitGroup;

        materials.dielectric = device.createDeviceLocalBuffer(scene.implicits.dielectric.materials.data(), BYTE_SIZE(scene.implicits.dielectric.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("dielectric_materials", materials.dielectric.buffer);
    }

    std::vector<rt::MeshObjectInstance> meshInstances;
    if(!scene.triangles.diffuse.objects.empty()) {
        const auto hitGroup = nextHitGroup();
        auto& instances = scene.triangles.diffuse.objects;
        for(auto& instance : instances) {
            instance.object.ensureMetadata();
            instance.hitGroupId = hitGroup * to<int>(RayType::Count);
        }
        meshInstances.insert(meshInstances.end(), instances.begin(), instances.end());

        scene.triangles.diffuse.hitGroup = hitGroup;
        triangleMaterials.diffuse = device.createDeviceLocalBuffer(scene.triangles.diffuse.materials.data(), BYTE_SIZE(scene.triangles.diffuse.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("triangle_diffuse_materials", triangleMaterials.diffuse.buffer);
    }

    if(!scene.triangles.metal.objects.empty()) {
        const auto hitGroup = nextHitGroup();
        auto& instances = scene.triangles.metal.objects;
        for(auto& instance : instances) {
            instance.object.ensureMetadata();
            instance.hitGroupId = hitGroup * to<int>(RayType::Count);
        }
        meshInstances.insert(meshInstances.end(), instances.begin(), instances.end());
        scene.triangles.metal.hitGroup = hitGroup;

        triangleMaterials.metal = device.createDeviceLocalBuffer(scene.triangles.metal.materials.data(), BYTE_SIZE(scene.triangles.metal.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("triangle_metal_materials", triangleMaterials.metal.buffer);
    }

    if(!scene.triangles.dielectric.objects.empty()) {
        const auto hitGroup = nextHitGroup();
        auto& instances = scene.triangles.dielectric.objects;
        for(auto& instance : instances) {
            instance.object.ensureMetadata();
            instance.hitGroupId = hitGroup * to<int>(RayType::Count);
        }
        meshInstances.insert(meshInstances.end(), instances.begin(), instances.end());
        scene.triangles.dielectric.hitGroup = hitGroup;

        triangleMaterials.dielectric = device.createDeviceLocalBuffer(scene.triangles.dielectric.materials.data(), BYTE_SIZE(scene.triangles.dielectric.materials), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("triangle_dielectric_materials", triangleMaterials.dielectric.buffer);
    }

    if(!scene.triangles.volume.objects.empty()) {
        const auto hitGroup = nextHitGroup();
        auto& instances = scene.triangles.volume.objects;
        for(auto& instance : instances) {
            instance.object.ensureMetadata();
            instance.hitGroupId = hitGroup * to<int>(RayType::Count);
        }
        meshInstances.insert(meshInstances.end(), instances.begin(), instances.end());
        scene.triangles.volume.hitGroup = hitGroup;

        mediums = device.createDeviceLocalBuffer(scene.triangles.volume.mediums.data(), BYTE_SIZE(scene.triangles.volume.mediums), usage);
        device.setName<VK_OBJECT_TYPE_BUFFER>("triangle_medium", mediums.buffer);
    }

    rtBuilder.add(meshInstances);

    createAccelerationStructure();
    uniforms.cpu->litBackGround = scene.litBackGround;
}

void RayTracingWeekendSeries::loadDefaultScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Default";
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, 0, -1}, 0.5);
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, -100.5, -1}, 100);


    scene.implicits.metal.spheres.emplace_back(glm::vec3(1, 0, -1), 0.5);
//    spheres.emplace_back(glm::vec3(-1, 0, -1), 0.5);


    scene.implicits.dielectric.spheres.emplace_back(glm::vec3(-1, 0, -1), 0.5);

    scene.implicits.diffuse.materials = { {{0.8, 0.3, 0.3}}, {{0.8, 0.8, 0.0}} };
    scene.implicits.metal.materials = { { {0.8, 0.6, 0.2}, 0.00 }, { {0.8, 0.8, 0.8}, 1 } };
    scene.implicits.dielectric.materials = { {1.5} };
}

void RayTracingWeekendSeries::loadTextureScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Texture";

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, -10, 1}, 10);
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, 10, 1}, 10);

    scene.implicits.diffuse.materials = { {glm::vec3(0), 0}, {glm::vec3(0), 0} };
}

void RayTracingWeekendSeries::loadPerlinNoiseScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Perlin noise";

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, 0, 0}, 1);
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, -1001, 0}, 1000);

    scene.implicits.diffuse.materials.resize(2);
    scene.implicits.diffuse.materials[0] = {
        .color = glm::vec3(0.6),
        .textureId = 2,
        .textureType = 1,
        .scale = 4
    };
    scene.implicits.diffuse.materials[1] = {
        .color = glm::vec3(0.6),
        .textureId = 2,
        .textureType = 1,
    };

}

void RayTracingWeekendSeries::loadLightScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Lights";

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, 2, 0}, 2);
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, 7, 0}, 2);
    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{0, -1000, 0}, 1000);

    scene.implicits.diffuse.materials.resize(3);
    scene.implicits.diffuse.materials[0] = {
        .color = glm::vec3(0.6),
        .textureId = 2,
        .textureType = 1,
        .scale = 4
    };
    scene.implicits.diffuse.materials[1] = {
        .emission = glm::vec3(1)
    };
    scene.implicits.diffuse.materials[2] = {
        .color = glm::vec3(0.6),
        .textureId = 2,
        .textureType = 1,
    };

    float x0 = 3;
    float x1 = 5;
    float y0 = 1;
    float y1 = 3;
    float z = -2;

    auto w = x1 - x0;
    auto h = y1 - y0;
    glm::vec3 c{ (x0 + x1) * .5, (y1 + y0) * .5, z };
    auto pose = glm::translate(glm::mat4{1}, c);
    auto& light = scene.triangles.diffuse.objects.emplace_back();
    light.object = rt::TriangleMesh{ &drawables["plane"] };
    light.xform = pose;
    light.xformIT = glm::inverse(pose);

    scene.triangles.diffuse.materials.resize(1);
    scene.triangles.diffuse.materials[0] = {
        .color = glm::vec3(1),
        .emission = glm::vec3(1)
    };

    scene.litBackGround = 0;

}

void RayTracingWeekendSeries::loadCornellBoxScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Cornell box";

    auto cornellBox = primitives::cornellBox();

    scene.triangles.diffuse.materials.resize(8);

    scene.triangles.diffuse.materials[0] = {
        .color = glm::vec3(0),
        .emission = glm::vec3(15)
    };

    scene.triangles.diffuse.materials[1] = {
        .color = cornellBox[3].vertices[0].color
    };

    scene.triangles.diffuse.materials[2] = {
        .color = cornellBox[1].vertices[0].color
    };

    scene.triangles.diffuse.materials[3] = {
        .color = cornellBox[7].vertices[0].color
    };

    scene.triangles.diffuse.materials[4] = {
        .color = cornellBox[2].vertices[0].color
    };

    scene.triangles.diffuse.materials[5] = {
        .color = cornellBox[4].vertices[0].color
    };

    scene.triangles.diffuse.materials[6] = {
        .color = cornellBox[6].vertices[0].color
    };

    scene.triangles.diffuse.materials[7] = {
        .color = cornellBox[5].vertices[0].color
    };

    auto& instance = scene.triangles.diffuse.objects.emplace_back();
    instance.object = rt::TriangleMesh{ &drawables["cornell_box"] };
//    meshes.resize(1);
//    meshes[0].name = "ShortBox";
//    meshes[0].vertices = cornellBox[6].vertices;
//    meshes[0].indices = cornellBox[6].indices;
//    scene.triangles.dielectric.materials.resize(1);
//    scene.triangles.dielectric.materials[0] = {
//            .ior = 1.5
//    };
//    phong::load(device, descriptorPool, scene.triangles.dielectric.objects, meshes, info);
//
//    meshes.resize(1);
//    meshes[0].name = "TallBox";
//    meshes[0].vertices = cornellBox[5].vertices;
//    meshes[0].indices = cornellBox[5].indices;
//    scene.triangles.metal.materials.resize(1);
//    scene.triangles.metal.materials[0] = {
//            .color = vec3(1),
//            .roughness = 0
//    };
//    phong::load(device, descriptorPool, scene.triangles.metal.objects, meshes, info);
    scene.litBackGround = 0;
}

void RayTracingWeekendSeries::loadCornellBoxVolumeScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Cornell box volumes";

    auto cornellBox = primitives::cornellBox();

    scene.triangles.diffuse.materials.resize(6);

    scene.triangles.diffuse.materials[0] = {
        .color = glm::vec3(0),
        .emission = glm::vec3(15)
    };

    scene.triangles.diffuse.materials[1] = {
        .color = cornellBox[3].vertices[0].color
    };

    scene.triangles.diffuse.materials[2] = {
        .color = cornellBox[1].vertices[0].color
    };

    scene.triangles.diffuse.materials[3] = {
        .color = cornellBox[7].vertices[0].color
    };

    scene.triangles.diffuse.materials[4] = {
        .color = cornellBox[2].vertices[0].color
    };

    scene.triangles.diffuse.materials[5] = {
        .color = cornellBox[4].vertices[0].color
    };

    auto& diffuseInstance = scene.triangles.diffuse.objects.emplace_back();
    diffuseInstance.object = rt::TriangleMesh{ &drawables["cornell_box"] };
    diffuseInstance.object.metaData[6].hitGroupId = ~0u;
    diffuseInstance.object.metaData[7].hitGroupId = ~0u;

    scene.triangles.volume.mediums.resize(2);
    scene.triangles.volume.mediums[0] = {
            .albedo = glm::vec3(1),
            .density = 0.1
    };

    scene.triangles.volume.mediums[1] = {
            .albedo = glm::vec3(0),
            .density = 0.1
    };

    auto& volumeInstance = scene.triangles.volume.objects.emplace_back();
    volumeInstance.object = rt::TriangleMesh{ &drawables["cornell_box"] };
    volumeInstance.object.metaData[6].customIndex = 0;
    volumeInstance.object.metaData[7].customIndex = 1;
    for(auto i = 0; i < 6; ++i) volumeInstance.object.metaData[i].hitGroupId = ~0u;

    scene.litBackGround = 0;
}

void RayTracingWeekendSeries::loadInOneWeekendScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Ray tracing in One weekend";

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3(0, -1000, 0), 1000);
    scene.implicits.diffuse.materials.resize(1);
    scene.implicits.diffuse.materials[0] = {
            .color = {0.5, 0, 0},
            .textureId = 0,
            .scale = 0.1,
            .useTriplanarMapping = 1
    };

    auto rand = rng(0.f, 1.f, 1 << 20);
    const auto n = 500;
    for(auto a = -11; a < 11; ++a) {
        for (auto b = -11; b < 11; ++b) {
            glm::vec3 center{a + 0.9 * rand(), 0.2, b + 0.9 * rand() };

            auto choose_mat = rand();
            if(choose_mat < 0.8) {
                scene.implicits.diffuse.spheres.emplace_back(center, 0.2);
                scene.implicits.diffuse.materials.push_back( { {rand() * rand(), rand() * rand(), rand() * rand()} });
            } else if (choose_mat < 0.95) {
                scene.implicits.metal.spheres.emplace_back(center, 0.2);
                scene.implicits.metal.materials.push_back({ {0.5 * (1 + rand()), 0.5 * (1 + rand()), 0.5 * (1 + rand())}, 0.5f * rand() });
            }else {
                scene.implicits.dielectric.spheres.emplace_back(center, 0.2);
                scene.implicits.dielectric.materials.push_back({1.5});
            }
        }
    }


    scene.implicits.dielectric.spheres.emplace_back(glm::vec3{0, 1, 0}, 1);
    scene.implicits.dielectric.materials.push_back({1.5});

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{-4, 1, 0}, 1);
    scene.implicits.diffuse.materials.push_back({{ 0.4, 0.2, 0.1}});

    scene.implicits.metal.spheres.emplace_back(glm::vec3{4, 1, 0}, 1);
    scene.implicits.metal.materials.push_back({{ 0.7, 0.6, 0.5}, 0.0});
}

void RayTracingWeekendSeries::loadInOneWeekendTrianglesScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "Ray tracing in One weekend, Triangle spheres";

    auto& bigSphere = scene.triangles.diffuse.objects.emplace_back();
    bigSphere.object = rt::TriangleMesh{ &drawables["uv_sphere"] };
    bigSphere.xform = glm::translate(glm::mat4{1}, {0, -1000, 0}) * glm::scale(glm::mat4{1}, glm::vec3(1000));
    bigSphere.xformIT = glm::inverse(bigSphere.xform);
    bigSphere.object.metaData.front().customIndex = scene.triangles.diffuse.materials.size();
    scene.triangles.diffuse.materials.resize(1);
    scene.triangles.diffuse.materials[0] = {
            .color = {0.5, 0, 0},
            .textureId = 0,
            .scale = 0.1,
            .useTriplanarMapping = 1
    };

    auto rand = rng(0.f, 1.f, 1 << 20);
    const auto n = 500;
    for(auto a = -11; a < 11; ++a) {
        for (auto b = -11; b < 11; ++b) {
            glm::vec3 center{a + 0.9 * rand(), 0.2, b + 0.9 * rand() };

            rt::MeshObjectInstance smallSphere{};
            smallSphere.object = rt::TriangleMesh{ &drawables["uv_sphere"] };
            smallSphere.xform = glm::translate(glm::mat4{1}, center) * glm::scale(glm::mat4{1}, glm::vec3(0.2));
            smallSphere.xformIT = glm::inverse(smallSphere.xform);

            auto choose_mat = rand();
            if(choose_mat < 0.8) {
                smallSphere.object.metaData.front().customIndex = scene.triangles.diffuse.materials.size();
                scene.triangles.diffuse.objects.push_back(std::move(smallSphere));
                scene.triangles.diffuse.materials.push_back( { {rand() * rand(), rand() * rand(), rand() * rand()} });
            } else if (choose_mat < 0.95) {
                smallSphere.object.metaData.front().customIndex = scene.triangles.metal.materials.size();
                scene.triangles.metal.objects.push_back(std::move(smallSphere));
                scene.triangles.metal.materials.push_back({ {0.5 * (1 + rand()), 0.5 * (1 + rand()), 0.5 * (1 + rand())}, 0.5f * rand() });
            }else {
                smallSphere.object.metaData.front().customIndex = scene.triangles.dielectric.materials.size();
                scene.triangles.dielectric.objects.push_back(std::move(smallSphere));
                scene.triangles.dielectric.materials.push_back({1.5});
            }
        }
    }


    auto& deMediumSphere = scene.triangles.dielectric.objects.emplace_back();
    deMediumSphere.object = rt::TriangleMesh{ &drawables["uv_sphere"] };
    deMediumSphere.xform = glm::translate(glm::mat4{1}, {0, 1, 0}) * glm::scale(glm::mat4{1}, glm::vec3(1));
    deMediumSphere.xformIT = glm::inverse(deMediumSphere.xform);
    deMediumSphere.object.metaData.front().customIndex = scene.triangles.dielectric.materials.size();
    scene.triangles.dielectric.materials.push_back({1.5});


    auto& dfMediumSphere = scene.triangles.diffuse.objects.emplace_back();
    dfMediumSphere.object = rt::TriangleMesh{ &drawables["hires_ico_sphere"] };
    dfMediumSphere.xform = glm::translate(glm::mat4{1}, {-4, 1, 0}) * glm::scale(glm::mat4{1}, glm::vec3(1));
    dfMediumSphere.xformIT = glm::inverse(dfMediumSphere.xform);
    dfMediumSphere.object.metaData.front().customIndex = scene.triangles.diffuse.materials.size();
    scene.triangles.diffuse.materials.push_back({{ 0.4, 0.2, 0.1}});

    auto& mtMediumSphere = scene.triangles.metal.objects.emplace_back();
    mtMediumSphere.object = rt::TriangleMesh{ &drawables["hires_ico_sphere"] };
    mtMediumSphere.xform = glm::translate(glm::mat4{1}, {4, 1, 0}) * glm::scale(glm::mat4{1}, glm::vec3(1));
    mtMediumSphere.xformIT = glm::inverse(mtMediumSphere.xform);
    mtMediumSphere.object.metaData.front().customIndex = scene.triangles.metal.materials.size();
    scene.triangles.metal.materials.push_back({{ 0.7, 0.6, 0.5}, 0.0});
}

void RayTracingWeekendSeries::loadTheNextWeekEndScene() {
    auto& scene = scenes.emplace_back();
    scene.name = "The next weekend";
    auto rand = rng(0.f, 1.f, 1 << 20);

    // boxes
    const auto nb = 20;
    for(auto i = 0; i < nb; ++i) {
        for(auto j = 0; j < nb; ++j) {
            glm::vec3 bmin, bmax;
            float w = 10;
            bmin.x = -100 + i*w;
            bmin.z = -100 + j*w;
            bmin.y = 0;
            bmax.x = bmin.x + w;
            bmax.y = 10 * (rand()+0.01);
            bmax.z = bmin.z + w;

            auto c = (bmin + bmax) * 0.5f;
            auto s = (bmax - bmin) * 0.5f;
            auto& instance = scene.triangles.diffuse.objects.emplace_back();
            instance.object = rt::TriangleMesh{ &drawables["cube"] };
            instance.object.metaData.front().customIndex = scene.triangles.diffuse.materials.size();
            instance.xform = glm::translate(glm::mat4{1}, c) * glm::scale(glm::mat4{1}, s);
        }
    }
    auto& groundMaterial = scene.triangles.diffuse.materials.emplace_back();
    groundMaterial.color = {0.48, 0.83, 0.53};

    // light
    glm::vec3 bmin{0}, bmax{0};
    bmin.x = 12.3;
    bmax.x = 42.3;
    bmin.y = 14.7;
    bmax.y = 41.2;
    bmin.z = 54.4;
    bmax.z = 55.4;
    auto c = (bmin + bmax) * 0.5f;
    auto s = (bmax - bmin) * 0.5f;
    c.xyz = c.xzy;
    auto& light = scene.triangles.diffuse.objects.emplace_back();
    light.object = rt::TriangleMesh{ &drawables["plane"] };
    light.object.metaData.front().customIndex = scene.triangles.diffuse.materials.size();
    light.xform = glm::translate(glm::mat4{1}, c) * glm::rotate(glm::mat4{1}, glm::half_pi<float>(), {1, 0, 0}) * glm::scale(glm::mat4{1}, s);

    auto& lightMaterial = scene.triangles.diffuse.materials.emplace_back();
    lightMaterial.emission = glm::vec3{7};
    lightMaterial.color = glm::vec3{1};

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3{40, 40, 20}, 5.f);
    scene.implicits.diffuse.materials.emplace_back(glm::vec3(0.7, 0.3, 0.1));

    scene.implicits.dielectric.spheres.emplace_back(glm::vec3{26, 15, 4.5}, 5.f);
    scene.implicits.dielectric.materials.emplace_back(1.5f);

    scene.implicits.metal.spheres.emplace_back(glm::vec3{0, 15, 14.5}, 5.f);
    scene.implicits.metal.materials.emplace_back(glm::vec3(0.8, 0.8, 0.9), 10.0);

    {
        auto &m1 = scene.triangles.volume.objects.emplace_back();
        m1.object = rt::TriangleMesh{&drawables["uv_sphere"]};
        m1.xform = glm::translate(glm::mat4{1}, {36, 15, 14.5}) * glm::scale(glm::mat4{1}, glm::vec3(6.5));
        scene.triangles.volume.mediums.emplace_back(glm::vec3(0.2, 0.4, 0.9), 0.2);

        scene.implicits.dielectric.spheres.emplace_back(glm::vec3(36, 15, 14.5), 7);
        scene.implicits.dielectric.materials.emplace_back(1.5);
    }

    // TODO ray inside medium
//    auto& m2 = scene.triangles.volume.objects.emplace_back();
//    m2.object = rt::TriangleMesh{ &drawables["uv_sphere"] };
//    m2.xform = glm::scale(glm::mat4{1}, glm::vec3(500));
//    scene.triangles.volume.mediums.emplace_back(glm::vec3(0), 10);

    scene.implicits.dielectric.spheres.emplace_back(glm::vec3(0), 500);
    scene.implicits.dielectric.materials.emplace_back(1.5);

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3(40, 20, 40), 10);
    scene.implicits.diffuse.materials.push_back({ .textureId = 1});

    scene.implicits.diffuse.spheres.emplace_back(glm::vec3(22, 28, 30), 8);
    scene.implicits.diffuse.materials.push_back({ .textureId = 2, .textureType = 1, .scale = 0.5});

    const auto ns = 1000;
    auto xform = glm::translate(glm::mat4{1}, {-10, 27, 39.5}) * glm::rotate(glm::mat4{1}, glm::radians(15.f), {0, 1, 0});
    for(auto j = 0; j < ns; ++j) {
        glm::vec3 center = (xform * glm::vec4(16.5 * rand(), 16.5 * rand(), 16.5 * rand(), 1)).xyz();
        scene.implicits.diffuse.spheres.emplace_back(center, 1);
        scene.implicits.diffuse.materials.push_back({ .color = glm::vec3(0.73)});
    }

    scene.litBackGround = 0;

}

void RayTracingWeekendSeries::initUniforms() {
    UniformData defaults{};
    uniforms.gpu = device.createCpuVisibleBuffer(&defaults, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData*>(uniforms.gpu.map());
}

void RayTracingWeekendSeries::endFrame() {
    uniforms.cpu->frame++;
    uniforms.cpu->viewInverse = glm::inverse(camera->cam().view);
    uniforms.cpu->projInverse = glm::inverse(camera->cam().proj);
    uniforms.cpu->cameraPosition = camera->position();
    uniforms.cpu->focalDistance = glm::distance(camera->position(), camera->target);

    if(uniforms.cpu->adaptiveSampling == 1) {
        uniforms.cpu->currentSample = glm::clamp(uniforms.cpu->currentSample, 0u, uniforms.cpu->sampleCount - 1);
        uniforms.cpu->currentSample++;
        if (camera->moved()) {
            uniforms.cpu->currentSample = 0;
        }
    }else if(uniforms.cpu->sampleCount > 64) {
        uniforms.cpu->sampleCount = 16;
    }
    if(sceneUpdated) {
        sceneUpdated = false;
        invalidateSwapChain();
    }
}

void RayTracingWeekendSeries::newFrame() {
    camera->newFrame();
}

void RayTracingWeekendSeries::loadTextures() {
    std::vector<std::string> paths;
    for(auto i = 0; i < 64; ++i) {
        paths.push_back(resource(std::format("fast_noise/128_128/uniform/RG_{}.png", i)));
    }
    noise.sampler = createNoiseSampler();
    textures::fromFile(device, noise, paths);

    textures::fromFile(device, earthTexture, resource("2k_earth_daymap.jpg"), true, VK_FORMAT_R8G8B8A8_SRGB);
    bindlessDescriptor.update({ &earthTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 });
}

VulkanSampler RayTracingWeekendSeries::createNoiseSampler() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode =  VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.maxLod = 1;

    return device.createSampler(samplerInfo);}

void RayTracingWeekendSeries::createCheckerboardTexture() {
    textures::checkerboard(device, checkerboard, glm::vec3(0.2, 0.3, 0.1), glm::vec3{0.9});
    bindlessDescriptor.update({ &checkerboard, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0});
}

void RayTracingWeekendSeries::computePerlinNoise() {
    textures::create(device, perlinNoise, VK_IMAGE_TYPE_3D, VK_FORMAT_R8G8B8A8_UNORM, {1024, 1024, 32});
    bindlessDescriptor.update({ &perlinNoise, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0, VK_IMAGE_LAYOUT_GENERAL});

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        Barriers::push(perlinNoise.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

        Barriers::flush(commandBuffer);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline("noise"));
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout("noise"), 0, 1, &bindlessDescriptor.descriptorSet, 0, nullptr);
        vkCmdDispatch(commandBuffer, 128, 128, 4);

        Barriers::push(perlinNoise.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                       VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        Barriers::flush(commandBuffer);

    });

    bindlessDescriptor.update({ &perlinNoise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2});

}

std::vector<PipelineMetaData> RayTracingWeekendSeries::pipelineMetaData() {
    return {
            {
                .name = "noise",
                .shadePath = resource("noise.comp.spv"),
                .layouts = { const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout)},
            },
            {
                .name = "accumulate_adaptive",
                .shadePath = resource("accumulate_adaptive.comp.spv"),
                .layouts = { &adaptiveSampling.descriptorSetLayout },
                .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(adaptiveSampling.constants)} }
            }
    };
}

uint32_t RayTracingWeekendSeries::nextHitGroup() {
    auto hitGroup = nextInstance++;
    return hitGroup;
}

void RayTracingWeekendSeries::loadMeshes() {
    createCornellBox();
    loadIcoSphere();
    loadUVSphere();
    loadCube();
    loadPlane();
}

void RayTracingWeekendSeries::createCornellBox() {
    auto cornellBox = primitives::cornellBox();

    std::vector<mesh::Mesh> meshes(8);

    meshes[0].name = "Light";
    meshes[0].vertices = cornellBox[0].vertices;
    meshes[0].indices = cornellBox[0].indices;

    meshes[1].name = "Floor";
    meshes[1].vertices = cornellBox[3].vertices;
    meshes[1].indices = cornellBox[3].indices;


    meshes[2].name = "Celling";
    meshes[2].vertices = cornellBox[1].vertices;
    meshes[2].indices = cornellBox[1].indices;


    meshes[3].name = "BackWall";
    meshes[3].vertices = cornellBox[7].vertices;
    meshes[3].indices = cornellBox[7].indices;


    meshes[4].name = "rightWall";
    meshes[4].vertices = cornellBox[2].vertices;
    meshes[4].indices = cornellBox[2].indices;

    meshes[5].name = "rightWall";
    meshes[5].vertices = cornellBox[4].vertices;
    meshes[5].indices = cornellBox[4].indices;

    meshes[6].name = "ShortBox";
    meshes[6].vertices = cornellBox[6].vertices;
    meshes[6].indices = cornellBox[6].indices;


    meshes[7].name = "TallBox";
    meshes[7].vertices = cornellBox[5].vertices;
    meshes[7].indices = cornellBox[5].indices;

    VulkanDrawable drawable;
    phong::load(device, descriptorPool, drawable, meshes, info);
    drawables.insert(std::make_pair("cornell_box", std::move(drawable)));
}

void RayTracingWeekendSeries::loadIcoSphere() {
    drawables.insert(std::make_pair("hires_ico_sphere", VulkanDrawable{}));
    phong::load(resource("ico_sphere.obj"), device, descriptorPool, drawables["hires_ico_sphere"], info);
}

void RayTracingWeekendSeries::loadUVSphere() {
    auto sphere = primitives::sphere(500, 500, 1, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    std::vector<mesh::Mesh> meshes(1);
    meshes.front().vertices = sphere.vertices;
    meshes.front().indices = sphere.indices;

    drawables.insert(std::make_pair("uv_sphere", VulkanDrawable{}));
    phong::load(device, descriptorPool, drawables["uv_sphere"], meshes, info);
}

void RayTracingWeekendSeries::loadCube() {
    auto cube = primitives::cube();
    std::vector<mesh::Mesh> meshes(1);
    meshes.front().vertices = cube.vertices;
    meshes.front().indices = cube.indices;

    drawables.insert(std::make_pair("cube", VulkanDrawable{}));
    phong::load(device, descriptorPool, drawables["cube"], meshes, info);
}

void RayTracingWeekendSeries::loadPlane() {
    auto plane = primitives::plane(2, 2, 2, 2, glm::mat4{1}, glm::vec4{1}, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    std::vector<mesh::Mesh> meshes(1);
    meshes.front().vertices = plane.vertices;
    meshes.front().indices = plane.indices;

    drawables.insert(std::make_pair("plane", VulkanDrawable{}));
    phong::load(device, descriptorPool, drawables["plane"], meshes, info);
}

void RayTracingWeekendSeries::createComputePipelines() {
    compute = std::make_unique<ComputePipelines>(&device, pipelineMetaData());
    compute->createPipelines();
}

void RayTracingWeekendSeries::loadForTheRestOfYourLife() {
    auto& scene = scenes.emplace_back();
    scene.name = "For the rest of your life";

    auto cornellBox = primitives::cornellBox();

    scene.triangles.diffuse.materials.resize(8);

    scene.triangles.diffuse.materials[0] = {
            .color = glm::vec3(0),
            .emission = glm::vec3(15)
    };

    scene.triangles.diffuse.materials[1] = {
            .color = cornellBox[3].vertices[0].color
    };

    scene.triangles.diffuse.materials[2] = {
            .color = cornellBox[1].vertices[0].color
    };

    scene.triangles.diffuse.materials[3] = {
            .color = cornellBox[7].vertices[0].color
    };

    scene.triangles.diffuse.materials[4] = {
            .color = cornellBox[2].vertices[0].color
    };

    scene.triangles.diffuse.materials[5] = {
            .color = cornellBox[4].vertices[0].color
    };

    scene.triangles.diffuse.materials[6] = {
            .color = cornellBox[6].vertices[0].color
    };

    scene.triangles.diffuse.materials[7] = {
            .color = cornellBox[5].vertices[0].color
    };

    auto& instance = scene.triangles.diffuse.objects.emplace_back();
    instance.object = rt::TriangleMesh{ &drawables["cornell_box"] };
    instance.object.metaData[6].hitGroupId = ~0u;


    scene.implicits.dielectric.spheres.emplace_back(glm::vec3(10, -19.2, 12), 8.25);
    scene.implicits.dielectric.materials.emplace_back(1.5);

    scene.litBackGround = 0;
}

void RayTracingWeekendSeries::accumulateAdaptive(VkCommandBuffer commandBuffer) {
    Barrier::rayTraceWriteToComputeRead(commandBuffer);

    const auto g = (glm::uvec2(width, height) + 1u)/8u;
    adaptiveSampling.constants.currentSample = uniforms.cpu->currentSample;
    adaptiveSampling.constants.sampleCount = uniforms.cpu->sampleCount;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline("accumulate_adaptive"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout("accumulate_adaptive"), 0, 1, &adaptiveSampling.descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute->layout("accumulate_adaptive"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(adaptiveSampling.constants), &adaptiveSampling.constants);
    vkCmdDispatch(commandBuffer, g.x, g.y, 1);

    Barrier::computeWriteToFragmentRead(commandBuffer);
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = RayTracingWeekendSeries{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}