#include "ShaderBindingTableDemo.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "color.hpp"


ShaderBindingTableDemo::ShaderBindingTableDemo(const Settings& settings)
: VulkanBaseApp("Shader Binding Table", settings)
, instanceDescriptions{ {0} }{
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("shader_binding_table");
    fileManager().addSearchPathFront("shader_binding_table/data");
    fileManager().addSearchPathFront("shader_binding_table/spv");
    fileManager().addSearchPathFront("shader_binding_table/models");
    fileManager().addSearchPathFront("shader_binding_table/textures");
    fileManager().addSearchPathFront(R"(C:\Users\joebh\OneDrive\media\models)");
}

void ShaderBindingTableDemo::initApp() {
    initCamera();
    createDisplay();
    createHitGroupColorBuffer();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    loadPrototypeModel();
    updateAS();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createRayTracingPipeline();
}

void ShaderBindingTableDemo::initCamera() {
    OrbitingCameraSettings cameraSettings;
//    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.orbitMinZoom = 0.1;
    cameraSettings.orbitMaxZoom = 512.0f;
    cameraSettings.offsetDistance = 1.0f;
    cameraSettings.modelHeight = 0.5;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);

    camera = std::make_unique<OrbitingCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    std::vector<glm::mat4> alloc(2);
    inverseCamProj = device.createCpuVisibleBuffer(alloc.data(), BYTE_SIZE(alloc), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    inverseCamera = inverseCamProj.span<glm::mat4>();
}

void ShaderBindingTableDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void ShaderBindingTableDemo::beforeDeviceCreation() {
    deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
    deviceExtensions.push_back(VK_KHR_RAY_TRACING_POSITION_FETCH_EXTENSION_NAME);

    auto enabledRayTracingPipelineFeatures = findExtension<VkPhysicalDeviceRayTracingPipelineFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, deviceCreateNextChain);
    enabledRayTracingPipelineFeatures->rayTracingPipeline = VK_TRUE;

    auto enabledAccelerationStructureFeatures = findExtension<VkPhysicalDeviceAccelerationStructureFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, deviceCreateNextChain);
    enabledAccelerationStructureFeatures->accelerationStructure = VK_TRUE;

    auto features12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    features12->scalarBlockLayout = VK_TRUE;
    features12->descriptorIndexing = VK_TRUE;
    features12->runtimeDescriptorArray = VK_TRUE;
    features12->bufferDeviceAddress = VK_TRUE;
    features12->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto rayFetch = findExtension<VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR, deviceCreateNextChain);
    rayFetch->rayTracingPositionFetch = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void ShaderBindingTableDemo::createDescriptorPool() {
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


void ShaderBindingTableDemo::createDescriptorSetLayouts() {
    raytrace.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("rtx_ac_descriptor_setLayout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .createLayout();

    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    vertexDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("vertex_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(3)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(3)
                .shaderStages(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
        .createLayout();

    hitGroupDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("hit_group_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void ShaderBindingTableDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ raytrace.descriptorSetLayout, textureDescriptorSetLayout, vertexDescriptorSetLayout, hitGroupDescriptorSetLayout });
    raytrace.descriptorSet = sets[0];
    display.descriptorSet = sets[1];
    vertexDescriptorSet = sets[2];
    hitGroupDescriptorSet = sets[3];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("rtx_as_descriptor_set", raytrace.descriptorSet);

    VkWriteDescriptorSetAccelerationStructureKHR accWrites{};
    accWrites.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    accWrites.accelerationStructureCount = 1;
    accWrites.pAccelerationStructures = &tlas.handle;

    auto writes = initializers::writeDescriptorSets<7>();
    writes[0].pNext = &accWrites;
    writes[0].dstSet = raytrace.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    writes[1].dstSet = raytrace.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    VkDescriptorImageInfo imageInfo{nullptr, display.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &imageInfo;

    writes[2].dstSet = raytrace.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    VkDescriptorBufferInfo bufferInfo{inverseCamProj.buffer, 0, VK_WHOLE_SIZE};
    writes[2].pBufferInfo = &bufferInfo;

    writes[3].dstSet = display.descriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorImageInfo displayInfo{display.texture.sampler.handle, display.texture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[3].pImageInfo = &displayInfo;

    std::vector<VkDescriptorBufferInfo> vertexBuffers{
        { vkBunny.vertexBuffer, 0, VK_WHOLE_SIZE },
    };

    writes[4].dstSet = vertexDescriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorCount = COUNT(vertexBuffers);
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].pBufferInfo = vertexBuffers.data();

    std::vector<VkDescriptorBufferInfo> indexBuffers{
        { vkBunny.indexBuffer, 0, VK_WHOLE_SIZE },
    };

    writes[5].dstSet = vertexDescriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorCount = COUNT(indexBuffers);
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].pBufferInfo = indexBuffers.data();

    writes[6].dstSet = hitGroupDescriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorCount = 1;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    VkDescriptorBufferInfo hitGroupInfo = { hitGroups.gpu, 0, VK_WHOLE_SIZE };
    writes[6].pBufferInfo = &hitGroupInfo;

    device.updateDescriptorSets(writes);
}

void ShaderBindingTableDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void ShaderBindingTableDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void ShaderBindingTableDemo::createRenderPipeline() {
    //    @formatter:off
        render.main.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("quad.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(textureDescriptorSetLayout)
                .name("display")
            .build(render.main.layout);

        render.hitGroup.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("hit_group.frag.spv"))
                .depthStencilState()
                    .compareOpAlways()
                .layout()
                    .addDescriptorSetLayout(hitGroupDescriptorSetLayout)
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int))
                .name("render_hit_groups")
            .build(render.hitGroup.layout);
    //    @formatter:on
}

void ShaderBindingTableDemo::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("main.rgen.spv"));
    auto missShaderModule = device.createShaderModule( resource("main.rmiss.spv"));
    auto hitShaderModule = device.createShaderModule( resource("main.rchit.spv"));

    auto shaders = std::vector<ShaderInfo>(to<int>(ShaderType::ShaderCount));
    shaders[to<int>(ShaderType::RayGen)] = { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR};
    shaders[to<int>(ShaderType::Miss)] = { missShaderModule, VK_SHADER_STAGE_MISS_BIT_KHR};
    shaders[to<int>(ShaderType::Hit)] = { hitShaderModule, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};

    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup());

    shaderGroups.push_back(shaderTablesDesc.addMissGroup(to<int>(ShaderType::Miss)));

    std::generate(hitGroups.cpu.begin(), hitGroups.cpu.begin() + numHitGroups, [rng=rng(0.f, 1.f, 1 << 20)] () mutable { return glm::vec4(rng(), rng(), rng(), 1); });
    for(auto i = 0; i < numHitGroups; ++i) {
        shaderGroups.push_back(shaderTablesDesc.addHitGroup(to<int>(ShaderType::Hit)));
        shaderTablesDesc.hitGroups.get(i).addRecord(hitGroups.cpu[i]);
    }

    auto stages = map_range(shaders, [](auto& shader){
        return VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = shader.stage,
            .module = shader.module.handle,
            .pName = shader.entry,
        };
    });


    raytrace.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout, vertexDescriptorSetLayout },
                                                  { {VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(constants)} });
    VkRayTracingPipelineCreateInfoKHR createInfo{ VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR };
    createInfo.stageCount = COUNT(stages);
    createInfo.pStages = stages.data();
    createInfo.groupCount = COUNT(shaderGroups);
    createInfo.pGroups = shaderGroups.data();
    createInfo.maxPipelineRayRecursionDepth = 0;
    createInfo.layout = raytrace.layout.handle;

    raytrace.pipeline = device.createRayTracingPipeline(createInfo);
    bindingTables = shaderTablesDesc.compile(device, raytrace.pipeline);
}


void ShaderBindingTableDemo::onSwapChainDispose() {
    dispose(render.main.pipeline);
    dispose(render.hitGroup.pipeline);
    dispose(raytrace.pipeline);
    descriptorPool.reset();
}

void ShaderBindingTableDemo::onSwapChainRecreation() {
    AppContext::onResize(swapChain, renderPass);
    camera->perspective(swapChain.aspectRatio());
    updateAS();
    createDisplay();
    updateDescriptorSets();
    createRenderPipeline();
    createRayTracingPipeline();
}

VkCommandBuffer *ShaderBindingTableDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        renderScene(commandBuffer);
        renderHitGroup(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);

    rayTrace(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void ShaderBindingTableDemo::renderScene(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.main.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.main.layout.handle, 0, 1, &display.descriptorSet, 0, nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void ShaderBindingTableDemo::renderHitGroup(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.hitGroup.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.hitGroup.layout.handle, 0, 1, &hitGroupDescriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, render.hitGroup.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &numHitGroups);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void ShaderBindingTableDemo::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Options");
    ImGui::SetWindowSize({0, 0});

    prevConstants = constants;
    int cullmask = to<int>(std::log2(constants.cullmask) + 0.5);
    ImGui::SliderInt("Cull Mask", &cullmask, 0, 8);

    int offset = to<int>(constants.offset);
    ImGui::SliderInt("SBT Record Offset", &offset, 0, 10);

    int stride = to<int>(constants.stride);
    ImGui::SliderInt("SBT Record Stride", &stride, 0, 10);

    int miss = to<int>(constants.miss);
    ImGui::SliderInt("Miss Stride", &miss, 0, 1);

    if(ImGui::CollapsingHeader("Scene setup", ImGuiTreeNodeFlags_DefaultOpen)) {
        for(auto i = 0; i < instanceDescriptions.size(); ++i) {
            auto& instance = instanceDescriptions[i];
            ImGui::Text("instance %d", i);
            ImGui::Indent(16);
            instanceUpdated |= ImGui::SliderInt(fmt::format("geometries##{}", i).c_str(), &instance.geometryCount, 1, 10);

            if(!autoComputeOffset) {
                instanceUpdated |= ImGui::SliderInt(fmt::format("offset##{}", i).c_str(), &instance.offset, 0, numHitGroups - 1);
            }
            ImGui::Indent(-16);
        }
        if(ImGui::Button("add instance")) {
            auto offset = autoComputeOffset ? nextOffset() : to<int>(instanceDescriptions.size());
            instanceDescriptions.emplace_back(offset);
            instanceUpdated = true;
        }
        instanceUpdated |= ImGui::Checkbox("auto compute offset:", &autoComputeOffset);
    }

    if(ImGui::CollapsingHeader("Hit Groups", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("hit groups: %d", numHitGroups);
        ImGui::SameLine();
        if(ImGui::Button("add hit group")) {
            addHitGroup = true;
        }
        ImGui::SameLine();
        if(numHitGroups > 1) {
            if(ImGui::Button("remove hit group")) {
                removeHitGroup = true;
            }
        }
    }

    if(ImGui::CollapsingHeader("results", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto rayOffset = to<int>(constants.offset);
        const auto rayStride = to<int>(constants.stride);

        for(auto desc : instanceDescriptions) {
            for(auto gid : std::ranges::iota_view(0, desc.geometryCount)) {
                auto group = computeHitGroup(desc.offset, gid, rayOffset, rayStride);
                if(group < numHitGroups) {
                    ImGui::TextColored({0, 1, 0, 1}, "%d", group);
                }else {
                    ImGui::TextColored({1, 0, 0, 1}, "%d", group);
                }
                if(gid < (desc.geometryCount - 1)) ImGui::SameLine();
            }
        }
    }
    if(!message.empty()) {
        ImGui::TextColored({1, 0, 0, 1}, "%s", message.c_str());
    }

    ImGui::End();


    plugin(IM_GUI_PLUGIN).draw(commandBuffer);

    constants.cullmask = (1 << cullmask) - 1;
    constants.offset = offset;
    constants.stride = stride;
    constants.miss = miss;

    auto maxHitGroup = computeMaxHitGroup() + 1;
    if(sbtIndexOutOfBounds()) {
        instanceUpdated = false;
        message = fmt::format("sbt stride/offset configuration of {}/{} requires {} hit groups", constants.offset, constants.stride, maxHitGroup);
        constants = prevConstants;
    }
}

void ShaderBindingTableDemo::rayTrace(VkCommandBuffer commandBuffer) {
    if(computeMaxHitGroup() >= numHitGroups) return;

    auto& disp = display.texture;
    Barriers::push(disp.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                   VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    Barriers::flush(commandBuffer);

    std::vector<VkDescriptorSet> sets{ raytrace.descriptorSet,  vertexDescriptorSet};
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, raytrace.layout.handle, VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(constants), &constants);
    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    Barriers::push(disp.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    Barriers::flush(commandBuffer);

}

void ShaderBindingTableDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()) {
        camera->update(time);
    }
    setTitle(fmt::format("{}, {}", title, "hitGroup formula: instanceShaderBindingTableDemoRecordOffset + geometryIndex × sbtRecordStride + sbtRecordOffset"));
}

void ShaderBindingTableDemo::checkAppInputs() {
    camera->processInput();
}

void ShaderBindingTableDemo::cleanup() {
    AppContext::shutdown();
    vkDestroyAccelerationStructureKHR(device, tlas.handle, nullptr);
}

void ShaderBindingTableDemo::onPause() {
    VulkanBaseApp::onPause();
}


void ShaderBindingTableDemo::loadTeaPot() {
    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= rtxUsage;
    info.indexUsage |= rtxUsage;
    info.vertexUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;

    auto geom = primitives::teapot();
    teapot = {
        .name = "teapot",
        .vertices = geom.vertices,
        .indices = geom.indices
    };

    std::vector<mesh::Mesh> meshes{ teapot };
    phong::load(device, descriptorPool, vkTeapot, meshes, info, true, 0.5);

    auto vertexBuffer = device.createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            VMA_MEMORY_USAGE_CPU_ONLY, vkTeapot.vertexBuffer.size);

    device.copy(vkTeapot.vertexBuffer, vertexBuffer, vkTeapot.vertexBuffer.size, 0, 0);
    auto offset = glm::vec3(0);
//    auto offset = (geometries[0].bounds.max - geometries[0].bounds.min);
//    offset += (geometries[1].bounds.max - geometries[1].bounds.min);

    auto transform = glm::translate(glm::mat4{1}, {offset.x, 0, 0});
    auto nMat = glm::mat3(glm::inverseTranspose(transform));

    auto vertices = reinterpret_cast<Vertex*>(vertexBuffer.map());
    auto size = vertexBuffer.size/sizeof(Vertex);

    for(auto i = 0; i < size; ++i) {
        vertices[i].position = transform * vertices[i].position;
        vertices[i].normal = nMat * vertices[i].normal;
    }

    vertexBuffer.unmap();
    device.copy(vertexBuffer, vkTeapot.vertexBuffer, vertexBuffer.size);
}

std::function<AsGeometryInfo(VulkanDrawable&)> ShaderBindingTableDemo::createAsGeometryFactory(const VulkanDevice &device) {
    return [&](VulkanDrawable& drawable) {
        VkDeviceOrHostAddressConstKHR vertexAddress{ .deviceAddress = device.getAddress(drawable.vertexBuffer) };
        VkDeviceOrHostAddressConstKHR indexAddress{ .deviceAddress =  device.getAddress(drawable.indexBuffer) };

        VkAccelerationStructureGeometryKHR asGeom{};
        asGeom.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        asGeom.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        asGeom.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        asGeom.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        asGeom.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        asGeom.geometry.triangles.vertexData = vertexAddress;
        asGeom.geometry.triangles.vertexStride = sizeof(Vertex);
        asGeom.geometry.triangles.maxVertex = drawable.numVertices() - 1;
        asGeom.geometry.triangles.indexType = VK_INDEX_TYPE_UINT32;
        asGeom.geometry.triangles.indexData = indexAddress;
        asGeom.geometry.triangles.transformData.deviceAddress = 0;
        asGeom.geometry.triangles.transformData.hostAddress = nullptr;

        AsBuildInfo buildInfo{};
        buildInfo.primitiveCount = drawable.numTriangles();
        buildInfo.primitiveOffset = 0;
        buildInfo.firstVertex = 0;
        buildInfo.transformOffset = 0;

        return AsGeometryInfo{ asGeom, buildInfo };
    };
}

void ShaderBindingTableDemo::createBLAS(GeomItr start, GeomItr end) {

    std::vector<AsGeom> geoms;
    std::vector<AsBuildInfo> infos;
    for(auto itr = start; itr != end; ++itr) {
        geoms.push_back(itr->geom);
        infos.push_back(itr->buildInfo);
    }

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR ;
    buildInfo.geometryCount = COUNT(geoms);
    buildInfo.pGeometries = geoms.data();

    auto numTriangles = map_range(infos, [](auto& bi){ return bi.primitiveCount; });

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, numTriangles.data(), &sizeInfo);

    auto& blas = blasData.emplace_back();
    blas.buffer = device.createBuffer(asUsage, VMA_MEMORY_USAGE_GPU_ONLY, sizeInfo.accelerationStructureSize);

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = blas.buffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    ERR_GUARD_VULKAN(vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &blas.handle));

    auto beforeAlignment = sizeInfo.buildScratchSize;
    ensureAlignmentScratchBufferSize(sizeInfo);
    spdlog::info("scratch size before: {}, after: {}", beforeAlignment, sizeInfo.buildScratchSize);

    auto scratchBuffer = createScratchBuffer(sizeInfo.buildScratchSize);
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = blas.handle;
    buildInfo.scratchData.deviceAddress = scratchBuffer.address;

    std::vector<const AsBuildInfo*> buildRangeInfos{ infos.data() };

    Synchronization sync{};
    sync._fence = device.createFence();
    sync._fence.reset();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &buildInfo, buildRangeInfos.data());
    }, sync);

    sync._fence.wait();

    VkAccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo{};
    asDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asDeviceAddressInfo.accelerationStructure = blas.handle;
    blas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &asDeviceAddressInfo);

}


void ShaderBindingTableDemo::ensureAlignmentScratchBufferSize(VkAccelerationStructureBuildSizesInfoKHR &info) const {
    auto props = getAccelerationStructureProperties();
    info.buildScratchSize = alignedSize(info.buildScratchSize, props.minAccelerationStructureScratchOffsetAlignment);
    info.buildScratchSize = nearestPowerOfTwo(info.buildScratchSize);
}

VkPhysicalDeviceAccelerationStructurePropertiesKHR ShaderBindingTableDemo::getAccelerationStructureProperties() const {
    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &asProps };
    vkGetPhysicalDeviceProperties2(device, &props);
    return asProps;
}

ScratchBuffer ShaderBindingTableDemo::createScratchBuffer(VkDeviceSize size) const {
    const auto minAlignment = getAccelerationStructureProperties().minAccelerationStructureScratchOffsetAlignment;

    ScratchBuffer scratchBuffer{};
    scratchBuffer.handle = device.createAlignedBuffer(
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            VMA_MEMORY_USAGE_GPU_ONLY,
            size, minAlignment,  "acceleration_struct_scratch_buffer");

    VkBufferDeviceAddressInfo bufferDeviceAddressInfo{};
    bufferDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferDeviceAddressInfo.buffer = scratchBuffer.handle;
    scratchBuffer.address = vkGetBufferDeviceAddress(device, &bufferDeviceAddressInfo);

    return scratchBuffer;
}


void ShaderBindingTableDemo::createTLAS() {
    auto offset = vkBunny.bounds.max - vkBunny.bounds.min;
    std::vector<VkAccelerationStructureInstanceKHR> instances{};
    for(auto i = 0; i < blasData.size(); ++i) {
        auto& blas = blasData[i];
        auto& instance = instances.emplace_back();
        instance.transform.matrix[0][0] = 1;
        instance.transform.matrix[1][1] = 1;
        instance.transform.matrix[2][2] = 1;
        instance.transform.matrix[1][3] = -(offset.y * 2) * i;
        instance.instanceCustomIndex = i;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = instanceDescriptions[i].offset;
        instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        instance.accelerationStructureReference = blas.deviceAddress;
    }

    auto numInstances = COUNT(instances);
    asInstances = device.createDeviceLocalBuffer(instances.data(), BYTE_SIZE(instances), rtxUsage);
    device.setName<VK_OBJECT_TYPE_BUFFER>("rtx_instances", asInstances.buffer);

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = device.getAddress(asInstances);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType =  VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    geometry.geometry.instances.data = instanceDataDeviceAddress;

    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{};
    accelerationStructureBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    accelerationStructureBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    accelerationStructureBuildGeometryInfo.geometryCount = 1;
    accelerationStructureBuildGeometryInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizesInfo{};
    sizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
            device,
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &accelerationStructureBuildGeometryInfo,
            &numInstances,
            &sizesInfo);

    tlas.buffer = device.createBuffer(
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
            VMA_MEMORY_USAGE_GPU_ONLY,
            sizesInfo.accelerationStructureSize
    );
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = tlas.buffer;
    createInfo.size = sizesInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &tlas.handle);

    auto bsize = sizesInfo.buildScratchSize;
    ensureAlignmentScratchBufferSize(sizesInfo);
    spdlog::info("scratch size before: {}, after: {}", bsize, sizesInfo.buildScratchSize);
    auto scratchBuffer = createScratchBuffer(sizesInfo.buildScratchSize);

    accelerationStructureBuildGeometryInfo.mode =  VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    accelerationStructureBuildGeometryInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = tlas.handle;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.address;

    VkAccelerationStructureBuildRangeInfoKHR  accelerationStructureBuildRangeInfo{};
    accelerationStructureBuildRangeInfo.primitiveCount = numInstances;
    accelerationStructureBuildRangeInfo.primitiveOffset = 0;
    accelerationStructureBuildRangeInfo.firstVertex = 0;
    accelerationStructureBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> accelerationBuildStructureRangeInfos = { &accelerationStructureBuildRangeInfo };

    Synchronization sync{};
    sync._fence = device.createFence();
    sync._fence.reset();

    device.computeCommandPool().oneTimeCommand( [&](auto commandBuffer){
        Barrier::rayTraceReadToAccelerationStructureUpdate(commandBuffer);
        vkCmdBuildAccelerationStructuresKHR(commandBuffer, 1, &accelerationStructureBuildGeometryInfo, accelerationBuildStructureRangeInfos.data());
        Barrier::accelerationStructureUpdateToRayTraceRead(commandBuffer);
    }, sync);

    sync._fence.wait();

    VkAccelerationStructureDeviceAddressInfoKHR accelerationStructureDeviceAddressInfo{};
    accelerationStructureDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    accelerationStructureDeviceAddressInfo.accelerationStructure = tlas.handle;
    tlas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &accelerationStructureDeviceAddressInfo);

}

void ShaderBindingTableDemo::createDisplay() {
    if(display.texture.isValid() && display.texture.width == width && display.texture.height == height){
        return;
    }
    std::vector<uint8_t> cb(width * height * 4);
    textures::checkerboard(cb.data(), {width, height});
    textures::create(device, display.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, cb.data(), {width, height, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void ShaderBindingTableDemo::endFrame() {
    auto cam = camera->camera;
    inverseCamera[0] = glm::inverse(cam.view);
    inverseCamera[1] = glm::inverse(cam.proj);

    if(!ImGui::IsAnyItemActive() && instanceUpdated || addHitGroup || removeHitGroup) {
        if(autoComputeOffset) {
            recomputeAllOffsets();
        }

        if(instanceUpdated) {
            auto nextHitGroupSize = numHitGroups;

            if(addHitGroup) nextHitGroupSize++;
            if(removeHitGroup) nextHitGroupSize--;

            const auto maxHitGroup = computeMaxHitGroup() + 1;

            if(maxHitGroup > nextHitGroupSize){
                recreateAS = false;
                message = fmt::format("you need {} hit groups to match your configuration", maxHitGroup);
            }else{
                recreateAS = instanceUpdated;
                instanceUpdated = false;
                message.clear();
            }
        }

        if(addHitGroup || removeHitGroup) {
            if (addHitGroup) {
                ++numHitGroups;
            }
            if (removeHitGroup) {
                --numHitGroups;
            }
            addHitGroup = false;
            removeHitGroup = false;
        }
        invalidateSwapChain();
    }
}


void ShaderBindingTableDemo::loadModel(VulkanDrawable& drawable, const glm::mat4& transform) {
    phong::VulkanDrawableInfo info{};
    info.vertexUsage |= rtxUsage;
    info.indexUsage |= rtxUsage;
    info.vertexUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    info.indexUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.materialUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.materialIdUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.generateMaterialId = true;

    std::vector<mesh::Mesh> meshes{ bunny };

    for(auto& vertex : meshes.front().vertices) {
        vertex.position = transform * vertex.position;
    }
    phong::load(device, descriptorPool, drawable, meshes, info, true, 1);

}

void ShaderBindingTableDemo::updateAS() {
    if(!recreateAS) return;
    destroyAS();

    glm::vec3 offset{};
    for(const auto& instDesc : instanceDescriptions) {
        glm::mat4 transform{1};
        std::vector<VulkanDrawable> geometries{};
        for(auto i = 0; i < instDesc.geometryCount; ++i) {
            auto& geometry = geometries.emplace_back();
            loadModel(geometry);
            geometry.apply(device, transform);
            auto d = geometry.bounds.max - geometry.bounds.min;
            transform = glm::translate(transform, {d.x * 1.25, 0, 0});
            offset.y -= d.y;
        }
        auto asGeometries = map_range(geometries, createAsGeometryFactory(device));
        createBLAS(asGeometries.begin(), asGeometries.end());
    }

    createTLAS();
    recreateAS = true;
    spdlog::warn("AS recreated");
}

void ShaderBindingTableDemo::destroyAS() {
    if(tlas.handle) {
        vkDestroyAccelerationStructureKHR(device, tlas.handle, nullptr);
        tlas.handle = nullptr;
    }
    for(auto& blas : blasData) {
        vkDestroyAccelerationStructureKHR(device, blas.handle, nullptr);
    }
    blasData.clear();
}

void ShaderBindingTableDemo::loadPrototypeModel() {
    std::vector<mesh::Mesh> meshes;
    mesh::load(meshes, resource("bunny.obj"));
    bunny = meshes.front();

    glm::mat4 pose{1};
    pose = glm::rotate(pose, -glm::half_pi<float>(), {1, 0, 0});

    for (auto& vertex : bunny.vertices) {
        vertex.normal *= -1;
    }

    auto nMat = glm::mat3(glm::inverseTranspose(pose));
    for(auto& vertex : bunny.vertices) {
        vertex.position = pose * vertex.position;
        vertex.normal = nMat * vertex.normal;
    }
    loadModel(vkBunny);
}

void ShaderBindingTableDemo::createHitGroupColorBuffer() {
    hitGroups.gpu = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::vec4) * 32, "hit_group_colors");
    hitGroups.cpu = hitGroups.gpu.span<glm::vec4>();
}

int ShaderBindingTableDemo::computeHitGroup(int instanceOffset, int geometryIndex, int rayOffset, int rayStride) {
    return instanceOffset + geometryIndex * rayStride + rayOffset;
}

int ShaderBindingTableDemo::computeMaxHitGroup() const {
    auto result = 0;
    const auto rayOffset = to<int>(constants.offset);
    const auto rayStride = to<int>(constants.stride);
    for(auto& desc : instanceDescriptions) {
        for(auto geometryIndex : std::views::iota(0, desc.geometryCount)) {
            auto group = computeHitGroup(desc.offset, geometryIndex, rayOffset, rayStride);
            result = std::max(result, group);
        }
    }
    return result;
}

void ShaderBindingTableDemo::recomputeAllOffsets() {
    instanceDescriptions.front().offset = 0;
    for(auto i = 1; i < instanceDescriptions.size(); ++i) {
        auto prevInstance = instanceDescriptions[i - 1];
        instanceDescriptions[i].offset = prevInstance.offset + prevInstance.geometryCount * 2;
    }
}

int ShaderBindingTableDemo::nextOffset() {
    assert(!instanceDescriptions.empty() && "instanceDescriptions should not be empty");
    auto prevInstance = instanceDescriptions.back();
    return prevInstance.offset + prevInstance.geometryCount * 2;

}

bool ShaderBindingTableDemo::sbtIndexOutOfBounds() const {
    const auto maxHitGroup = computeMaxHitGroup() + 1;
    const auto pc = prevConstants;
    auto &c = constants;
    auto diff = std::memcmp(&pc, &c, sizeof(c));
    return std::memcmp(&pc, &c, sizeof(c)) != 0 && maxHitGroup > numHitGroups;
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1080;
        settings.height = 720;
        settings.depthTest = true;
        settings.uniqueQueueFlags |= VK_QUEUE_COMPUTE_BIT;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = ShaderBindingTableDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}