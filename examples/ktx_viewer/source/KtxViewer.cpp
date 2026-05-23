#include "KtxViewer.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

KtxViewer::KtxViewer(const Settings& settings) : VulkanBaseApp("KTX Viewer", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("ktx_viewer");
    fileManager().addSearchPathFront("ktx_viewer/data");
    fileManager().addSearchPathFront("ktx_viewer/spv");
    fileManager().addSearchPathFront("ktx_viewer/models");
    fileManager().addSearchPathFront("ktx_viewer/textures");
    fileManager().addSearchPathFront("erosion/saves/terrain");
}

void KtxViewer::initApp() {
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    loadTextures();
    initCanvas();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void KtxViewer::initCamera() {
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

void KtxViewer::loadTextures() {
    textures::fromFile(device, displacementMap, resource("displacement.ktx"));
    textures::fromFile(device, normalMap, resource("normals.ktx"));
    textures::fromFile(device, moment0, resource("slope_moments0.ktx"));
    textures::fromFile(device, moment1, resource("slope_moments1.ktx"));

    auto subresource = DEFAULT_SUB_RANGE;
    subresource.aspectMask = displacementMap.aspectMask;
    subresource.layerCount = displacementMap.layers;
    subresource.levelCount = displacementMap.levels;
    displacementMap.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, subresource);

    subresource.aspectMask = normalMap.aspectMask;
    subresource.layerCount = normalMap.layers;
    subresource.levelCount = normalMap.levels;
    normalMap.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, subresource);

    subresource.aspectMask = moment0.aspectMask;
    subresource.layerCount = moment0.layers;
    subresource.levelCount = moment0.levels;
    moment0.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, subresource);

    subresource.aspectMask = moment1.aspectMask;
    subresource.layerCount = moment1.layers;
    subresource.levelCount = moment1.levels;
    moment1.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, subresource);
}

void KtxViewer::initCanvas() {
    canvas = Canvas{this};
    canvas.init();
}

void KtxViewer::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void KtxViewer::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;

    AppContext::addExtensions(deviceCreateNextChain);
}

void KtxViewer::createDescriptorPool() {
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


void KtxViewer::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void KtxViewer::createDescriptorSetLayouts() {
    textureDescriptorSetLayout =
    device.descriptorSetLayoutBuilder()
        .name("texture_descriptor_set_layout")
        .binding(0)
            .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
            .descriptorCount(1)
            .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
    .createLayout();
}

void KtxViewer::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { textureDescriptorSetLayout, textureDescriptorSetLayout,
        textureDescriptorSetLayout, textureDescriptorSetLayout });
    descriptorSet = sets[0];
    normalMapDescriptorSet = sets[1];
    slopeMoment0DescriptorSet = sets[2];
    slopeMoment1DescriptorSet = sets[3];

    auto writes = initializers::writeDescriptorSets<4>();
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo dispInfo{ displacementMap.sampler.handle, displacementMap.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[0].pImageInfo = &dispInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = normalMapDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo normalInfo{ normalMap.sampler.handle, normalMap.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[1].pImageInfo = &normalInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = slopeMoment0DescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo slope0fo{ moment0.sampler.handle, moment0.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &slope0fo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = slopeMoment1DescriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo slope1Info{ moment1.sampler.handle, moment1.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[3].pImageInfo = &slope1Info;

    device.updateDescriptorSets(writes);
}

void KtxViewer::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void KtxViewer::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void KtxViewer::createRenderPipeline() {
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


void KtxViewer::onSwapChainDispose() {
    dispose(render.pipeline);
}

void KtxViewer::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

void KtxViewer::textureViewerControls() {
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
    ImVec2 imageSize{ static_cast<float>(width), static_cast<float>(height) - 100 };

    ImGui::Image(id->second, imageSize);
}

VkCommandBuffer *KtxViewer::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{
        canvas.draw(commandBuffer, slopeMoment0DescriptorSet);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void KtxViewer::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void KtxViewer::checkAppInputs() {
    camera->processInput();
}

void KtxViewer::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void KtxViewer::onPause() {
    VulkanBaseApp::onPause();
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
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = KtxViewer{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}