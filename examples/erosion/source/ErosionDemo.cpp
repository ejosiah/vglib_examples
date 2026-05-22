#include "ErosionDemo.hpp"
#include "ErodedTerrain.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

#include <algorithm>

#include "sun_calc.hpp"

#include <exception>

namespace {
    constexpr glm::ivec2 TerrainWorldSize{10000};
    constexpr glm::vec2 TerrainHeightScale{-1.0f, 1059.0f};
    constexpr uint32_t ControlPanelWidth = 450;
    constexpr uint32_t MinSceneWidth = 320;
}

ErosionDemo::ErosionDemo(const Settings& settings) : VulkanBaseApp("Erosion", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/textures/height_map");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("common/spv");
    fileManager().addSearchPathFront("erosion");
    fileManager().addSearchPathFront("erosion/data");
    fileManager().addSearchPathFront("erosion/spv");
    fileManager().addSearchPathFront("erosion/models");
    fileManager().addSearchPathFront("erosion/textures");
}

void ErosionDemo::initApp() {
    const auto [lat, lng] = geo_location::get();
    currentPosition = {lat, lng};
    updateSunPosition();
    createSamplers();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    initContext();
    initGBuffer();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    initDisplacementMapGenerator();
    initSim();
    initAtmosphere();
    initTerrain();
    initDisplacementShadowMap();
    clearColor(0, 0, 1);
}

void ErosionDemo::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.zFar = 10000 * km;
    cameraSettings.zNear = 1;
    cameraSettings.acceleration = glm::vec3(1 * km);
    cameraSettings.velocity = glm::vec3(10 * km);
    const auto extent = sceneExtent();
    cameraSettings.aspectRatio = static_cast<float>(extent.x) / static_cast<float>(extent.y);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    auto pos = glm::vec3{-2272, 25, -517};
    auto target = pos + glm::vec3{-0.5, 0.13, 0.8};
     camera->lookAt(pos, target, {0, 1, 0});
}

void ErosionDemo::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void ErosionDemo::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    devFeatures12->scalarBlockLayout = VK_TRUE;

    auto localReads = findExtension<VkPhysicalDeviceDynamicRenderingLocalReadFeaturesKHR>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_LOCAL_READ_FEATURES_KHR, deviceCreateNextChain);
    localReads->dynamicRenderingLocalRead = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void ErosionDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 200;
    std::array<VkDescriptorPoolSize, 5> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}


void ErosionDemo::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void ErosionDemo::createDescriptorSetLayouts() {
    displayDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("display_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    context.subpassInputDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("input_attachment_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

}

void ErosionDemo::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ displayDescriptorSetLayout, context.subpassInputDescriptorSetLayout });
    displayDescriptorSet = sets[0];
    context.subpassInputDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<4>();

    writes[0].dstSet = displayDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo displayInfo{ renderGraphInputs.color.sampler.handle, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &displayInfo;

    writes[1].dstSet = context.subpassInputDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo subpassColorInfo{ VK_NULL_HANDLE, renderGraphInputs.color.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[1].pImageInfo = &subpassColorInfo;

    writes[2].dstSet = context.subpassInputDescriptorSet;
    writes[2].dstBinding = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo subpassPosInfo{ VK_NULL_HANDLE, renderGraphInputs.position.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[2].pImageInfo = &subpassPosInfo;

    writes[3].dstSet = context.subpassInputDescriptorSet;
    writes[3].dstBinding = 2;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo subpassDepthInfo{ VK_NULL_HANDLE, renderGraphInputs.depth.imageView.handle, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR };
    writes[3].pImageInfo = &subpassDepthInfo;

    device.updateDescriptorSets(writes);
}

void ErosionDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void ErosionDemo::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void ErosionDemo::createRenderPipeline() {
    const auto extent = sceneExtent();
    const auto scissorWidth = static_cast<int32_t>(extent.x);
    const auto scissorHeight = static_cast<int32_t>(extent.y);

    //    @formatter:off
    render.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("quad.frag.spv"))
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(extent.x, extent.y)
                .scissor()
                    .offset(0, 0)
                    .extent(scissorWidth, scissorHeight)
                .add()
            .dynamicState()
                .viewport()
                .scissor()
            .layout()
                .addDescriptorSetLayout(displayDescriptorSetLayout)
            .name("lighting")
        .build(render.layout);

    toneMapper.pipeline =
        prototypes->cloneScreenSpaceGraphicsPipeline()
            .shaderStage()
                .vertexShader(resource("quad.vert.spv"))
                .fragmentShader(resource("tone_mapping.frag.spv"))
            .dynamicRenderPass()
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .addColorAttachment(VK_FORMAT_R32G32B32A32_SFLOAT)
                .depthAttachment(VK_FORMAT_D16_UNORM)
            .depthStencilState()
                .compareOpAlways()
            .viewportState().clear()
                .viewport()
                    .origin(0, 0)
                    .dimension(extent.x, extent.y)
                .scissor()
                    .offset(0, 0)
                    .extent(scissorWidth, scissorHeight)
                .add()
            .dynamicState()
                .viewport()
                .scissor()
            .colorBlendState()
                .attachments(2)
            .layout()
                .addDescriptorSetLayout(context.subpassInputDescriptorSetLayout)
                .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapper.constants))
            .name("tone_mapper")
        .build(toneMapper.layout);

    //    @formatter:on
}


void ErosionDemo::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(toneMapper.pipeline);
    dispose(renderGraphInputs.color.imageView);
    dispose(renderGraphInputs.color.image);
    dispose(renderGraphInputs.position.imageView);
    dispose(renderGraphInputs.position.image);
    dispose(renderGraphInputs.depth.imageView);
    dispose(renderGraphInputs.depth.image);
    dispose(renderGraphInputs.depth1.imageView);
    dispose(renderGraphInputs.depth1.image);
}

void ErosionDemo::onSwapChainRecreation() {
    const auto extent = sceneExtent();
    context.screenWidth = extent.x;
    context.screenHeight = extent.y;
    camera->perspective(static_cast<float>(extent.x) / static_cast<float>(extent.y));
    initGBuffer();
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *ErosionDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    processTerrainMapSave();

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    if(displacementMapGenerator->regenerateIfNeeded(commandBuffer)) {
        backupOriginalTerrain(commandBuffer);
        erosionSim->update(commandBuffer, displacementMapGenerator->displacementTexture());
    }
    runSim(commandBuffer);
    applyTerrainMapBinding();
    displacementShadowMap->setDisplacementScale(terrain->displacementScale());
    displacementShadowMap->exec(commandBuffer);
    atmosphere->preProcess(commandBuffer);
    terrain->preProcess(commandBuffer);

    runRenderGraph(commandBuffer);

    renderToSwapChain([&]{
        renderToDisplay(commandBuffer);
        terrain->renderTopView(commandBuffer);
        renderUI(commandBuffer);
    }, commandBuffer);


    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}


void ErosionDemo::runRenderGraph(VkCommandBuffer commandBuffer) {
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
    Offscreen::render(commandBuffer, renderInfo, [&]{
        setSceneViewport(commandBuffer);
        terrain->render(commandBuffer);
        atmosphere->renderSkyView(commandBuffer);
        localReadBarrier(commandBuffer);
        if(atmosphere->arealPerspectiveEnabled()) {
            atmosphere->renderArealPerspective(commandBuffer);
            localReadBarrier(commandBuffer);
        }
        toneMap(commandBuffer);
    });
    Barriers::pushAndFlush(commandBuffer, renderGraphInputs.color.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void ErosionDemo::runSim(VkCommandBuffer commandBuffer) {
    auto& displacementTexture = displacementMapGenerator->displacementTexture();
    if(erosionSim->step(commandBuffer, displacementTexture) != ErosionSimulator::StepResult::Idle) {
        displacementMapGenerator->refreshDerivedMaps(commandBuffer);
        if(options.visualizeWaterFlow) {
            Barrier::computeWriteToFragmentRead(commandBuffer);
        }
    }
}

void ErosionDemo::processTerrainMapSave() {
    if(!terrainMapSave.requested) {
        return;
    }

    terrainMapSave.requested = false;
    try {
        const auto savePath = displacementMapGenerator->saveTerrainMaps(terrainMapSave.path.data());
        terrainMapSave.status = fmt::format("Saved {}", savePath.string());
        terrainMapSave.error = false;
    }catch(const std::exception& err) {
        terrainMapSave.status = fmt::format("Save failed: {}", err.what());
        terrainMapSave.error = true;
    }
}

void ErosionDemo::backupOriginalTerrain(VkCommandBuffer commandBuffer) {
    auto backupTexture = [&](Texture& destination, Texture& source) {
        source.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        const bool recreate = !destination.isValid()
            || destination.format != source.format
            || destination.width != source.width
            || destination.height != source.height
            || destination.depth != source.depth
            || destination.levels != source.levels;

        if(recreate) {
            textureViewer.imguiTextureIds.erase(&destination);
            dispose(destination);
            destination.levels = source.levels;
            destination.layers = source.layers;
            textures::create(device, destination, VK_IMAGE_TYPE_2D, source.format, {source.width, source.height, source.depth}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        }

        textures::copy(commandBuffer, source, destination);
        if(destination.levels > 1) {
            textures::generateLOD(commandBuffer, destination.image, destination.width, destination.height, destination.levels, destination.layers);
        }
        destination.image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    };

    backupTexture(originalTerrain.displacement, displacementMapGenerator->displacementTexture());
    backupTexture(originalTerrain.normals, displacementMapGenerator->normalTexture());
    backupTexture(originalTerrain.slopeMoments0, displacementMapGenerator->slopeMoments0Texture());
    backupTexture(originalTerrain.slopeMoments1, displacementMapGenerator->slopeMoments1Texture());
    originalTerrain.ready = true;
}

void ErosionDemo::applyTerrainMapBinding() {
    if(options.showOriginalTerrain && !originalTerrain.ready) {
        options.showOriginalTerrain = false;
    }

    const bool useOriginal = options.showOriginalTerrain && originalTerrain.ready;
    auto& displacement = useOriginal ? originalTerrain.displacement : displacementMapGenerator->displacementTexture();
    auto& normals = useOriginal ? originalTerrain.normals : displacementMapGenerator->normalTexture();
    auto& slopeMoments0 = useOriginal ? originalTerrain.slopeMoments0 : displacementMapGenerator->slopeMoments0Texture();
    auto& slopeMoments1 = useOriginal ? originalTerrain.slopeMoments1 : displacementMapGenerator->slopeMoments1Texture();

    bindlessDescriptor.update({&displacement, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.dmap_tex_index});
    bindlessDescriptor.update({&normals, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.dmap_normal_tex_index});
    bindlessDescriptor.update({&slopeMoments0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.dmap_slope_moments0_tex_index});
    bindlessDescriptor.update({&slopeMoments1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.dmap_slope_moments1_tex_index});
}

glm::uvec2 ErosionDemo::sceneExtent() const {
    const auto swapchainWidth = swapChain.width();
    const auto swapchainHeight = swapChain.height();
    const auto sceneWidth = swapchainWidth > ControlPanelWidth + MinSceneWidth
        ? swapchainWidth - ControlPanelWidth
        : std::max(1u, swapchainWidth);
    return {sceneWidth, std::max(1u, swapchainHeight)};
}

void ErosionDemo::setSceneViewport(VkCommandBuffer commandBuffer) const {
    const auto extent = sceneExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.x);
    viewport.height = static_cast<float>(extent.y);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent.width = extent.x;
    scissor.extent.height = extent.y;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void ErosionDemo::renderToDisplay(VkCommandBuffer commandBuffer) {
    setSceneViewport(commandBuffer);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &displayDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void ErosionDemo::renderUI(VkCommandBuffer commandBuffer) {
    const auto& io = ImGui::GetIO();
    const auto panelWidth = static_cast<float>(ControlPanelWidth);
    ImGui::SetNextWindowPos({std::max(0.0f, io.DisplaySize.x - panelWidth), 0.0f}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({std::min(panelWidth, io.DisplaySize.x), io.DisplaySize.y}, ImGuiCond_Always);

    constexpr ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("Controls", nullptr, flags);
    if(ImGui::BeginTabBar("ErosionDemoTabs")) {
        if(ImGui::BeginTabItem("Terrain")) {
            ImGui::Checkbox("Show original terrain", &options.showOriginalTerrain);
            ImGui::Separator();
            ImGui::InputText("Save path", terrainMapSave.path.data(), terrainMapSave.path.size());
            if(ImGui::Button("Save maps")) {
                terrainMapSave.requested = true;
            }
            if(!terrainMapSave.status.empty()) {
                const auto color = terrainMapSave.error ? ImVec4{1.0f, 0.25f, 0.2f, 1.0f} : ImVec4{0.3f, 0.9f, 0.45f, 1.0f};
                ImGui::TextColored(color, "%s", terrainMapSave.status.c_str());
            }
            ImGui::Separator();
            terrain->controlsContent();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Displacement")) {
            displacementMapGenerator->controlsContent();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Erosion")) {
            erosionSim->controlsContent();
            ImGui::Separator();
            ImGui::Checkbox("Visualize water flow", &options.visualizeWaterFlow);
            ImGui::SliderFloat("Flow color scale", &options.waterFlowScale, 0.1f, 512.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Textures")) {
            textureViewerControls();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Atmosphere")) {
            atmosphere->controlsContent();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Lighting")) {
            ImGui::SliderFloat("Zenith Angle", &options.lightZenith, -90, 180);
            ImGui::SliderFloat("Azimuth Angle", &options.lightAzimuth, 0, 360);
            ImGui::Checkbox("Dynamic light", &options.dynamicLight);
            terrain->lightingControls();
            displacementShadowMap->controls();

            if (ImGui::CollapsingHeader("ToneMapping", ImGuiTreeNodeFlags_DefaultOpen)) {
                static std::array<const char *, 5> labels{"Clamp", "Reinhard", "Uncharted 2", "ACES",
                                                          "Hejl-Burgess-Dawson"};
                ImGui::Combo("Tone mapper", &toneMapper.constants.method, labels.data(), labels.size());
                ImGui::SliderFloat("Exposure Value", &toneMapper.constants.exposureValue, -3, 3);
            }
            ImGui::Checkbox("Debug", &options.debug);
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Performance")) {
            auto total = 0.0f;
            total += displacementShadowMap->printPerfStats();
            total += terrain->printPerfStats();
            total += atmosphere->printPerfStats();

            ImGui::Text("total frame time: %f ms", total);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void ErosionDemo::textureViewerControls() {
    std::vector<BindlessTexture> textures;
    for(const auto& texture : bindlessDescriptor.boundedTextures) {
        if(texture.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER && texture.texture && texture.texture->isValid()) {
            textures.push_back(texture);
        }
    }

    std::sort(textures.begin(), textures.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.index < rhs.index;
    });

    if(textures.empty()) {
        ImGui::Text("No bound sampled textures");
        return;
    }

    auto slotForPosition = [&](int position) {
        const auto clampedPosition = std::clamp(position, 0, static_cast<int>(textures.size()) - 1);
        return static_cast<int>(textures[clampedPosition].index);
    };

    auto current = std::find_if(textures.begin(), textures.end(), [&](const auto& texture) {
        return static_cast<int>(texture.index) == textureViewer.textureSlot;
    });

    if(current == textures.end()) {
        textureViewer.textureSlot = slotForPosition(0);
        current = textures.begin();
    }

    const auto currentPosition = static_cast<int>(std::distance(textures.begin(), current));

    if(ImGui::Button("-")) {
        textureViewer.textureSlot = slotForPosition(currentPosition - 1);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if(ImGui::InputInt("Slot", &textureViewer.textureSlot, 1, 8)) {
        const auto nearest = std::lower_bound(textures.begin(), textures.end(), textureViewer.textureSlot, [](const auto& texture, int slot) {
            return static_cast<int>(texture.index) < slot;
        });
        if(nearest == textures.end()) {
            textureViewer.textureSlot = slotForPosition(static_cast<int>(textures.size()) - 1);
        }else {
            textureViewer.textureSlot = static_cast<int>(nearest->index);
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("+")) {
        textureViewer.textureSlot = slotForPosition(currentPosition + 1);
    }

    current = std::find_if(textures.begin(), textures.end(), [&](const auto& texture) {
        return static_cast<int>(texture.index) == textureViewer.textureSlot;
    });
    if(current == textures.end()) {
        current = textures.begin();
        textureViewer.textureSlot = static_cast<int>(current->index);
    }

    const auto* texture = current->texture;
    ImGui::Text("Binding %u  %ux%u", current->index, texture->width, texture->height);

    auto id = textureViewer.imguiTextureIds.find(texture);
    if(id == textureViewer.imguiTextureIds.end()) {
        id = textureViewer.imguiTextureIds
            .emplace(texture, plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(*const_cast<Texture*>(texture), current->imageLayout))
            .first;
    }

    const auto available = ImGui::GetContentRegionAvail();
    const auto maxSide = std::max(available.x, 64.0f);
    const auto aspect = texture->height > 0 ? static_cast<float>(texture->width) / static_cast<float>(texture->height) : 1.0f;
    ImVec2 imageSize{maxSide, maxSide / std::max(aspect, 0.0001f)};
    if(imageSize.y > 512.0f) {
        imageSize.y = 512.0f;
        imageSize.x = imageSize.y * aspect;
    }
    ImGui::Image(id->second, imageSize);
}

void ErosionDemo::update(float time) {
    if(!ImGui::IsAnyItemActive()){
        camera->update(time);
    }
    context.elapsedTime = time;
    // setTitle(fmt::format("{}, camera - {}, direction - {}, lightDirection - {}, nodes - {}, FPS - {}", title, camera->position(), camera->viewDir, lightDirection, terrain->nodeCount(), framePerSecond));
    setTitle(fmt::format("{}, nodes - {}, FPS - {}", title, terrain->nodeCount(), framePerSecond));

//    static auto g = glm::vec3{0, -9.8 * m, 0};
//    static auto v = glm::vec3{0};;
//    v += g * time;
//    camera->position(camera->position() + v * time);
}

void ErosionDemo::checkAppInputs() {
    camera->processInput();
    if(ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        context.mouse = glm::ivec4{mouse.position, 1, 0};
    }else {
        context.mouse = glm::ivec4{0};
    }
}

void ErosionDemo::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void ErosionDemo::onPause() {
    VulkanBaseApp::onPause();
}

void ErosionDemo::initContext() {
    const auto extent = sceneExtent();
    context.screenWidth = extent.x;
    context.screenHeight = extent.y;
    context.mouseInput = &mouse;
    context.device = &device;
    context.descriptorPool = &descriptorPool;
    context.camera = camera.get();
    context.rgInputs = &renderGraphInputs;
    context.edgeClampSampler = edgeClampSampler;
    context.bindlessDescriptor = &bindlessDescriptor;
    context.prototypes = std::make_unique<Prototypes>(device, swapChain, renderPass);
    context.lightDirection = glm::normalize(glm::vec3{1});
    context.dmap_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_normal_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_slope_moments0_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_slope_moments1_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.dmap_shadow_tex_index = bindlessDescriptor.reserveTextureSlots(1);
    context.transmittanceTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.multiScatteringTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.skyViewTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.arealPerspectiveTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.radianceTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.positionTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.depthTextureIndex = bindlessDescriptor.reserveTextureSlots(1);
    context.profiler = &nullProfiler;
}

void ErosionDemo::initTerrain() {
    terrain = std::make_unique<ErodedTerrain>(context, atmosphere->descriptor(), TerrainWorldSize, TerrainHeightScale);
    terrain->init();
}

void ErosionDemo::initDisplacementMapGenerator() {
    auto path = "generated_displacement.png";
    displacementMapGenerator = std::make_unique<DisplacementMapGenerator>(context, DisplacementMethod::Noise, 4096, 4096, resource(path));
    displacementMapGenerator->setTerrainMetrics(glm::vec2{static_cast<float>(TerrainWorldSize.x), static_cast<float>(TerrainWorldSize.y)}, TerrainHeightScale);
    displacementMapGenerator->init();
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        backupOriginalTerrain(commandBuffer);
    });
}

void ErosionDemo::initDisplacementShadowMap() {
    auto dmapInfo = displacementMapGenerator->displacementMapInfo();
    auto terrainInfo = terrain->getInfo();

    displacementShadowMap = std::make_unique<DisplacementShadowMap>(context, dmapInfo, terrainInfo);
    displacementShadowMap->init();
}

void ErosionDemo::initAtmosphere() {
    atmosphere = std::make_unique<AtmosphereModel>(context);
    atmosphere->init();
}

void ErosionDemo::initSim() {
    auto dmapInfo = displacementMapGenerator->displacementMapInfo();
    erosionSim = std::make_unique<ErosionSimulator>(
        context,
        glm::ivec2{dmapInfo.width, dmapInfo.height},
        glm::vec2{static_cast<float>(TerrainWorldSize.x), static_cast<float>(TerrainWorldSize.y)},
        TerrainHeightScale.y - TerrainHeightScale.x
    );
    erosionSim->init();
}


void ErosionDemo::endFrame() {
    terrain->endFrame();
    updateSunPosition();
}

void ErosionDemo::updateSunPosition()  {
    if (!options.dynamicLight) return;

    auto now = std::chrono::system_clock::now();
    auto pos = sun_calc::get_position(now, currentPosition.x, currentPosition.y);
    options.lightZenith = glm::degrees(pos.altitude);
    options.lightAzimuth = glm::radians(pos.azimuth);
}

void ErosionDemo::newFrame() {
    camera->newFrame();
    auto& cam = camera->cam();

    glm::mat4 rot = glm::rotate(glm::mat4{1}, glm::radians(options.lightAzimuth), {0, 1, 0});
    rot = glm::rotate(rot, glm::radians(options.lightZenith), {0, 0, 1});
    lightDirection = (rot * glm::vec4{1, 0, 0, 1}).xyz();

    context.lightDirection = lightDirection;
    context.view = cam.view;
    context.viewProjection = cam.proj * cam.view;
    context.inverseViewProjection = glm::inverse(context.viewProjection);
    context.inverseView = glm::inverse(cam.view);
    context.inverseProjection = glm::inverse(cam.proj);
    Frustum::extractFrustum(context.viewProjectionFrustum, context.viewProjection);

    atmosphere->newFrame();
    terrain->newFrame();
    terrain->setWaterFlowVisualization(options.visualizeWaterFlow, erosionSim->velocityFieldTextureIndex(), options.waterFlowScale);
}

void ErosionDemo::initGBuffer() {
    const auto extent = sceneExtent();
    const auto width = extent.x;
    const auto height = extent.y;

    textures::create(device, renderGraphInputs.color, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, renderGraphInputs.position, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {width, height, 1});
    textures::create(device, renderGraphInputs.depth, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});
    textures::create(device, renderGraphInputs.depth1, VK_IMAGE_TYPE_2D, VK_FORMAT_D16_UNORM, {width, height, 1});

    bindlessDescriptor.update({&renderGraphInputs.color, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.radianceTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.position, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.positionTextureIndex});
    bindlessDescriptor.update({&renderGraphInputs.depth, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, context.depthTextureIndex});

    renderInfo = Offscreen::RenderInfo{
            .colorAttachments = {
                    {renderGraphInputs.color.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
                    {renderGraphInputs.position.imageView, VK_FORMAT_R32G32B32A32_SFLOAT},
            },
            .depthAttachment = {{renderGraphInputs.depth.imageView, VK_FORMAT_D16_UNORM}},
            .renderArea = {width, height}
    };

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        auto subresource = DEFAULT_SUB_RANGE;
        subresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        Barriers::push(renderGraphInputs.depth.image, subresource, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
        Barriers::push(renderGraphInputs.position.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_RENDERING_LOCAL_READ_KHR);
        Barriers::flush(commandBuffer);
    });
}


void ErosionDemo::localReadBarrier(VkCommandBuffer commandBuffer) {
    Barriers::push(
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    Barriers::flush(commandBuffer, VK_DEPENDENCY_BY_REGION_BIT);

}

void ErosionDemo::toneMap(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, toneMapper.pipeline.handle);
    vkCmdPushConstants(commandBuffer, toneMapper.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(toneMapper.constants), &toneMapper.constants);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, toneMapper.layout.handle, 0, 1, &context.subpassInputDescriptorSet, 0,nullptr);
    AppContext::renderClipSpaceQuad(commandBuffer);
}

void ErosionDemo::createSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.maxLod = 1;

    edgeClampSampler = device.createSampler(samplerInfo);
}

int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1080 + ControlPanelWidth;
        settings.height = 720;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.enabledFeatures.geometryShader = true;
        settings.enabledFeatures.tessellationShader = true;
        settings.enabledFeatures.independentBlend = true;
        settings.enabledFeatures.pipelineStatisticsQuery = true;
        settings.enabledFeatures.occlusionQueryPrecise = true;
        settings.enabledFeatures.shaderStorageImageExtendedFormats = VK_TRUE;
        settings.enabledFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = ErosionDemo{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}
