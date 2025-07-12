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
}

void RayTracingWeekendSeries::initApp() {
    initCamera();
    initCanvas();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    createCheckerboardTexture();
    createNoiseTexture();
    initLoader();
    loadScene();
    createMaterials();
    initUniforms();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createRayTracingPipeline();
}

void RayTracingWeekendSeries::initCamera() {
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

void RayTracingWeekendSeries::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
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
            .binding(3) // noise
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void RayTracingWeekendSeries::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { raytrace.descriptorSetLayout });
    raytrace.descriptorSet = sets[0];

    auto writes = initializers::writeDescriptorSets<4>();

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

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = raytrace.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo noiseInfo{ noise.sampler.handle, noise.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[3].pImageInfo = &noiseInfo;

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
    canvas = Canvas{ this, imageUsage, VK_FORMAT_R8G8B8A8_UNORM, {}, resource("display.frag.spv")};
    canvas.init();
}

void RayTracingWeekendSeries::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));
    auto missShaderModule = device.createShaderModule( resource("main.rmiss.spv"));
    auto diffuseHitShaderModule = device.createShaderModule( resource("diffuse.rchit.spv"));
    auto metalHitShaderModule = device.createShaderModule( resource("metal.rchit.spv"));
    auto dielectricHitShaderModule = device.createShaderModule( resource("dielectric.rchit.spv"));
    auto implicitsIntersectShaderModule = device.createShaderModule( resource("main.rint.spv"));

    auto shaders = std::vector<ShaderInfo>(to<int>(ShaderIndex::Count));
    shaders[to<int>(ShaderIndex::RayGen)] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[to<int>(ShaderIndex::Miss)] = { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[to<int>(ShaderIndex::DiffuseHit)] = { diffuseHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::MetalHit)] = { metalHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::DielectricHit)] = { dielectricHitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    shaders[to<int>(ShaderIndex::Implicits)] = { implicitsIntersectShaderModule, VK_SHADER_STAGE_INTERSECTION_BIT_KHR};


    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup());

    shaderGroups.push_back(shaderTablesDesc.addMissGroup(to<int>(ShaderIndex::Miss)));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DiffuseHit), to<int>(ShaderIndex::Implicits), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Diffuse)].addRecord(device.getAddress(diffuseSpheres));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Diffuse)].addRecord(device.getAddress(materials.diffuse));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::MetalHit), to<int>(ShaderIndex::Implicits), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Metal)].addRecord(device.getAddress(metalSpheres));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Metal)].addRecord(device.getAddress(materials.metal));

    shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderIndex::DielectricHit), to<int>(ShaderIndex::Implicits), VK_SHADER_UNUSED_KHR, VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Dielectric)].addRecord(device.getAddress(dielectricSpheres));
    shaderTablesDesc.hitGroups[to<int>(HitShaders::Dielectric)].addRecord(device.getAddress(materials.dielectric));


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

    rayTraceToCanvasBarrier(commandBuffer);
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
}

void RayTracingWeekendSeries::onSwapChainRecreation() {
    initCanvas();
    createRayTracingPipeline();
    updateDescriptorSets();
    createRenderPipeline();
    uniforms.cpu->currentSample = 0;
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

    if(ImGui::CollapsingHeader("sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
        if(uniforms.cpu->adaptiveSampling == 0) {
            samples = std::clamp(samples, 1, 64);
            dirty |= ImGui::SliderInt("sample count", &samples, 1, 64);
        }else {
            samples = std::clamp(samples, 1000, 1000000);
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
    loadInOneWeekendScene();
}

void RayTracingWeekendSeries::loadDefaultScene() {
    constexpr auto usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    std::vector<imp::Sphere> spheres;

    spheres.emplace_back(glm::vec3{0, 0, -1}, 0.5);
    spheres.emplace_back(glm::vec3{0, -100.5, -1}, 100);

    diffuseSpheres = device.createDeviceLocalBuffer(spheres.data(), BYTE_SIZE(spheres), usage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("diffuse_spheres", diffuseSpheres.buffer);
    rtBuilder.add(spheres, 0, 0);


    spheres.clear();
    spheres.emplace_back(glm::vec3(1, 0, -1), 0.5);
//    spheres.emplace_back(glm::vec3(-1, 0, -1), 0.5);
    metalSpheres = device.createDeviceLocalBuffer(spheres.data(), BYTE_SIZE(spheres), usage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("metal_spheres", metalSpheres.buffer);
    rtBuilder.add(spheres, 0, 1);

    spheres.clear();
    spheres.emplace_back(glm::vec3(-1, 0, -1), 0.5);
    dielectricSpheres = device.createDeviceLocalBuffer(spheres.data(), BYTE_SIZE(spheres), usage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("dielectric_spheres", dielectricSpheres.buffer);
    rtBuilder.add(spheres, 0, 2);

    asInstances = rtBuilder.buildTlas();

    mattes = { {{0.8, 0.3, 0.3}}, {{0.8, 0.8, 0.0}} };
    metals = { { {0.8, 0.6, 0.2}, 0.00 }, { {0.8, 0.8, 0.8}, 1 } };
    dielectrics = { {1.5} };
}

void RayTracingWeekendSeries::loadInOneWeekendScene() {

    constexpr auto usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT  | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    std::vector<imp::Sphere> difSpheres;
    std::vector<imp::Sphere> matSpheres;
    std::vector<imp::Sphere> dieSpheres;


    difSpheres.emplace_back(glm::vec3(0, -1000, 0), 1000);
    mattes.push_back({ glm::vec3(0.5), 0 });

    auto rand = rng(0.f, 1.f, 1 << 20);
    const auto n = 500;
    for(auto a = -11; a < 11; ++a) {
        for (auto b = -11; b < 11; ++b) {
            glm::vec3 center{a + 0.9 * rand(), 0.2, b + 0.9 * rand() };

            auto choose_mat = rand();
            if(choose_mat < 0.8) {
                difSpheres.emplace_back(center, 0.2);
                mattes.push_back( { {rand() * rand(), rand() * rand(), rand() * rand()} });
            } else if (choose_mat < 0.95) {
                matSpheres.emplace_back(center, 0.2);
                metals.push_back({ {0.5 * (1 + rand()), 0.5 * (1 + rand()), 0.5 * (1 + rand())}, 0.5f * rand() });
            }else {
                dieSpheres.emplace_back(center, 0.2);
                dielectrics.push_back({1.5});
            }
        }
    }


    dieSpheres.emplace_back(glm::vec3{0, 1, 0}, 1);
    dielectrics.push_back({1.5});

    difSpheres.emplace_back(glm::vec3{-4, 1, 0}, 1);
    mattes.push_back({{ 0.4, 0.2, 0.1}});

    matSpheres.emplace_back(glm::vec3{4, 1, 0}, 1);
    metals.push_back({{ 0.7, 0.6, 0.5}, 0.0});

    rtBuilder.add(difSpheres, 0, 0);
    rtBuilder.add(matSpheres, 0, 1);
    rtBuilder.add(dieSpheres, 0, 2);
    createAccelerationStructure();

    diffuseSpheres = device.createDeviceLocalBuffer(difSpheres.data(), BYTE_SIZE(difSpheres), usage);
    metalSpheres = device.createDeviceLocalBuffer(matSpheres.data(), BYTE_SIZE(matSpheres), usage);
    dielectricSpheres = device.createDeviceLocalBuffer(dieSpheres.data(), BYTE_SIZE(dieSpheres), usage);
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
}

void RayTracingWeekendSeries::newFrame() {
    camera->newFrame();
}

void RayTracingWeekendSeries::createMaterials() {
    constexpr auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    materials.diffuse = device.createDeviceLocalBuffer(mattes.data(), BYTE_SIZE(mattes), usage);
    materials.metal = device.createDeviceLocalBuffer(metals.data(), BYTE_SIZE(metals), usage);
    materials.dielectric = device.createDeviceLocalBuffer(dielectrics.data(), BYTE_SIZE(dielectrics), usage);

    device.setName<VK_OBJECT_TYPE_BUFFER>("diffuse_materials", materials.diffuse.buffer);
    device.setName<VK_OBJECT_TYPE_BUFFER>("metal_materials", materials.metal.buffer);
    device.setName<VK_OBJECT_TYPE_BUFFER>("dielectric_materials", materials.dielectric.buffer);
}

void RayTracingWeekendSeries::createNoiseTexture() {
    std::vector<std::string> paths;
    for(auto i = 0; i < 64; ++i) {
        paths.push_back(resource(std::format("fast_noise/128_128/uniform/RG_{}.png", i)));
    }
    noise.sampler = createNoiseSampler();
    textures::fromFile(device, noise, paths);
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