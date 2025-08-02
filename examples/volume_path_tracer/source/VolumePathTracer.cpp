#include "VolumePathTracer.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

VolumePathTracer::VolumePathTracer(const Settings& settings) : VulkanRayTraceBaseApp("Volume Path tracer", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/textures/environment");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("volume_path_tracer");
    fileManager().addSearchPathFront("volume_path_tracer/data");
    fileManager().addSearchPathFront("volume_path_tracer/spv");
    fileManager().addSearchPathFront("volume_path_tracer/models");
    fileManager().addSearchPathFront("volume_path_tracer/textures");
}

void VolumePathTracer::initApp() {
    initCamera();
    initCanvas();
    initUniforms();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    loadEnvironment();
    initObjectData();
    loadModels();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createRayTracingPipeline();
}

void VolumePathTracer::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
}

void VolumePathTracer::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void VolumePathTracer::newFrame() {
    camera->newFrame();
}

void VolumePathTracer::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto posFetchFeature = findExtension<VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR, deviceCreateNextChain);
    posFetchFeature->rayTracingPositionFetch = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void VolumePathTracer::createDescriptorPool() {
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


void VolumePathTracer::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void VolumePathTracer::createDescriptorSetLayouts() {
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
        .createLayout();

    object.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("object_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void VolumePathTracer::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { raytrace.descriptorSetLayout, object.descriptorSetLayout });
    raytrace.descriptorSet = sets[0];
    object.descriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<6>();

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
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, canvas.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;

    writes[3].dstSet = object.descriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    VkDescriptorBufferInfo surfaceInfo{ object.surface.gpu, 0, VK_WHOLE_SIZE};
    writes[3].pBufferInfo = &surfaceInfo;

    writes[4].dstSet = object.descriptorSet;
    writes[4].dstBinding = 1;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    VkDescriptorBufferInfo materialInfo{ object.materials.gpu, 0, VK_WHOLE_SIZE};
    writes[4].pBufferInfo = &materialInfo;

    writes[5].dstSet = object.descriptorSet;
    writes[5].dstBinding = 2;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    VkDescriptorBufferInfo mediumInfo{ object.mediums.gpu, 0, VK_WHOLE_SIZE};
    writes[5].pBufferInfo = &mediumInfo;

    device.updateDescriptorSets(writes);
}

void VolumePathTracer::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void VolumePathTracer::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void VolumePathTracer::initCanvas() {
    const auto imageUsage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    canvas = Canvas{ this, imageUsage, VK_FORMAT_R32G32B32A32_SFLOAT, {}, resource("render.frag.spv")};
    canvas.init();

}

void VolumePathTracer::initUniforms() {
    UniformData data{};
    uniforms.gpu = device.createCpuVisibleBuffer(&data, sizeof(UniformData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = reinterpret_cast<UniformData*>(uniforms.gpu.map());

    device.setName<VK_OBJECT_TYPE_BUFFER>("uniforms", uniforms.gpu.buffer);
}

void VolumePathTracer::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("main.rgen.spv"));
    auto missShaderModule = device.createShaderModule( resource("main.rmiss.spv"));
    auto chitShaderModule = device.createShaderModule( resource("main.rchit.spv"));

    auto stages = initializers::rayTraceShaderStages({
         { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR},
         { chitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR},
                                                     });
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup());
    shaderGroups.push_back(shaderTablesDesc.addMissGroup(1));
    shaderGroups.push_back(shaderTablesDesc.addHitGroup(2));

    dispose(raytrace.layout);

    raytrace.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout, *bindlessDescriptor.descriptorSetLayout, object.descriptorSetLayout });
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

void VolumePathTracer::rayTrace(VkCommandBuffer commandBuffer) {
    Barrier::fragmentReadToComputeWrite(commandBuffer);

    std::vector<VkDescriptorSet> sets{ raytrace.descriptorSet, bindlessDescriptor.descriptorSet, object.descriptorSet };
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    Barrier::rayTraceWriteToFragmentRead(commandBuffer);
}

void VolumePathTracer::createRenderPipeline() {
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


void VolumePathTracer::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(raytrace.pipeline);
}

void VolumePathTracer::onSwapChainRecreation() {
    initCanvas();
    createRayTracingPipeline();
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *VolumePathTracer::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        canvas.draw(commandBuffer);
    }, commandBuffer);

    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void VolumePathTracer::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void VolumePathTracer::checkAppInputs() {
    camera->processInput();
}

void VolumePathTracer::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void VolumePathTracer::onPause() {
    VulkanBaseApp::onPause();
}

void VolumePathTracer::loadModels() {
    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;
    info.vertexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage += VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialBufferMemoryUsage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    VulkanDrawable drawable;

    phong::load(resource("plane.gltf"), device, descriptorPool, drawable,  info);
    drawables.insert(std::make_pair("plane", std::move(drawable)));
    rt::MeshObjectInstance planeInstance{};
    planeInstance.object = rt::TriangleMesh{ &drawables["plane"] };

    object.surface.cpu[instances.size()] =  { object.materials.count++ };
    object.materials.cpu[object.surface.cpu[instances.size()].materialId] = { .diffuse{-1} };
    instances.push_back(planeInstance);



    phong::load(resource("baby_dragon.obj"), device, descriptorPool, drawable, info, true);
    drawables.insert(std::make_pair("dragon", std::move(drawable)));
    float y =  -drawables["dragon"].bounds.min.y;
    rt::MeshObjectInstance dragon{ &drawables["dragon"] };
    dragon.xform = glm::translate(glm::mat4{1}, { 0, y, 0});

    object.surface.cpu[instances.size()] = { object.materials.count++ };
    object.materials.cpu[object.surface.cpu[instances.size()].materialId] = { .diffuse{0.8, 0.2, 0.01} };
    instances.push_back(dragon);

    createAccelerationStructure(instances);
}

void VolumePathTracer::endFrame() {
    uniforms.cpu->frame++;
    uniforms.cpu->viewInverse = glm::inverse(camera->cam().view);
    uniforms.cpu->projInverse = glm::inverse(camera->cam().proj);

    uniforms.cpu->currentSample = glm::clamp(uniforms.cpu->currentSample, 0u, uniforms.cpu->sampleCount - 1);
    uniforms.cpu->currentSample++;
    if (camera->moved()) {
        uniforms.cpu->currentSample = 0;
    }
}

void VolumePathTracer::loadEnvironment() {
    environment = textures::equirectangularToOctahedralMap(device, resource("sky.exr"), 2048);
    uniforms.cpu->environment_id = bindlessDescriptor.update(environment, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void VolumePathTracer::initObjectData() {
    object.materials.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(MaterialInfo) * object.maxObjects, "materials");
    object.mediums.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(MediumInfo) * object.maxObjects, "mediums");
    object.surface.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(SurfaceInfo) * object.maxObjects, "surface");
    object.materials.cpu = reinterpret_cast<MaterialInfo*>(object.materials.gpu.map());
    object.mediums.cpu = reinterpret_cast<MediumInfo*>(object.mediums.gpu.map());
    object.surface.cpu = reinterpret_cast<SurfaceInfo*>(object.surface.gpu.map());
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
        settings.deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = VolumePathTracer{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}