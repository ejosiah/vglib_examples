#include "LinearlyTransformedCosines.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"
#include "ltc.hpp"

LinearlyTransformedCosines::LinearlyTransformedCosines(const Settings& settings) : VulkanBaseApp("Linearly Transformed Cosines", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/ltc");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("ltc");
    fileManager().addSearchPathFront("ltc/data");
    fileManager().addSearchPathFront("ltc/spv");
    fileManager().addSearchPathFront("ltc/models");
    fileManager().addSearchPathFront("ltc/textures");
}

void LinearlyTransformedCosines::initApp() {
    initUniforms();
    loadLtcTextures();
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
}

void LinearlyTransformedCosines::initCamera() {
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

void LinearlyTransformedCosines::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void LinearlyTransformedCosines::beforeDeviceCreation() {
    auto devFeatures12 = findExtension<VkPhysicalDeviceVulkan12Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, deviceCreateNextChain);
    if (devFeatures12.has_value()) {
        devFeatures12.value()->scalarBlockLayout = VK_TRUE;
        devFeatures12.value()->shaderOutputViewportIndex = VK_TRUE;
    } else {
        static VkPhysicalDeviceVulkan12Features devFeatures12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        devFeatures12.scalarBlockLayout = VK_TRUE;
        devFeatures12.shaderOutputViewportIndex = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures12);
    }

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
        devFeatures13.value()->maintenance4 = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        devFeatures13.maintenance4 = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);

    static VkPhysicalDeviceIndexTypeUint8FeaturesEXT indexType8{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT };
    indexType8.indexTypeUint8 = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, indexType8);
}

void LinearlyTransformedCosines::createDescriptorPool() {
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


void LinearlyTransformedCosines::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void LinearlyTransformedCosines::createDescriptorSetLayouts() {
    ltcDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ltc_textures_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
    
    uniformsDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ltc_uniforms_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void LinearlyTransformedCosines::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ ltcDescriptorSetLayout, uniformsDescriptorSetLayout });
    ltcDescriptorSet = sets[0];
    uniformsDescriptorSet = sets[1];

    auto writes = initializers::writeDescriptorSets<3>();

    writes[0].dstSet = ltcDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo ltc_mat_info{ ltc_mat.sampler.handle, ltc_mat.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[0].pImageInfo = &ltc_mat_info;

    writes[1].dstSet = ltcDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo ltc_mag_info{ ltc_mag.sampler.handle, ltc_mag.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[1].pImageInfo = &ltc_mag_info;

    writes[2].dstSet = uniformsDescriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[2].descriptorCount = 1;
    VkDescriptorBufferInfo uniformsInfo{ uniforms.gpu, 0, VK_WHOLE_SIZE };
    writes[2].pBufferInfo = &uniformsInfo;

    device.updateDescriptorSets(writes);
}

void LinearlyTransformedCosines::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void LinearlyTransformedCosines::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void LinearlyTransformedCosines::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.ltc.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("ltc.frag.spv"))
                .layout()
                    .addDescriptorSetLayout(ltcDescriptorSetLayout)
                    .addDescriptorSetLayout(uniformsDescriptorSetLayout)
                .name("ltc_reference")
                .build(render.ltc.layout);
    //    @formatter:on
}


void LinearlyTransformedCosines::onSwapChainDispose() {
    dispose(render.ltc.pipeline);
}

void LinearlyTransformedCosines::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *LinearlyTransformedCosines::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    renderToSwapChain([&]{
        static std::array<VkDescriptorSet, 2> sets;
        sets[0] = ltcDescriptorSet;
        sets[1] = uniformsDescriptorSet;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ltc.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ltc.layout.handle, 0, COUNT(sets), sets.data(), 0, nullptr);
        AppContext::renderClipSpaceQuad(commandBuffer);

        renderControls(commandBuffer);
    }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void LinearlyTransformedCosines::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    updateView();
}

void LinearlyTransformedCosines::checkAppInputs() {
    camera->processInput();

    static bool initialPress = true;
    static glm::vec2 pos{};

    if(!ImGui::IsAnyItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if(initialPress) {
            initialPress = false;
            pos.x = ImGui::GetMousePos().x;
            pos.y = ImGui::GetMousePos().y;
        }
        cam.rotY = ImGui::GetMousePos().x - pos.x;
        cam.rotX = ImGui::GetMousePos().y - pos.y;
    }else {
        initialPress = true;
        pos = glm::vec2(0);
    }
    cam.zoom += 10 * ImGui::GetIO().MouseWheel;
}

void LinearlyTransformedCosines::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void LinearlyTransformedCosines::onPause() {
    VulkanBaseApp::onPause();
}

void LinearlyTransformedCosines::loadLtcTextures() {
    
    auto loadTexture = [this](auto& texture, auto path, auto w, auto h, auto format) {
        textures::createNoTransition(device, texture, VK_IMAGE_TYPE_2D, format, {w, h, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        auto stagingBuffer = device.createStagingBuffer(w * h * sizeof(float) * 4);
        auto data = load(path);
        stagingBuffer.copy(data);
        
        device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
            Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_NONE, 
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL); 
            
            Barriers::flush(commandBuffer);

            VkBufferImageCopy region{
                .imageSubresource = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = 0,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                },
                .imageOffset = {0, 0, 0},
                .imageExtent = {w, h, 1}
            };
            vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            Barriers::push(texture.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            Barriers::flush(commandBuffer);
        });
        
    };

    loadTexture(ltc_mag, "ltc_mag.dat", 64u, 64u, VK_FORMAT_R32_SFLOAT);
    loadTexture(ltc_mat, "ltc_mat.dat", 64u, 64u, VK_FORMAT_R32G32B32A32_SFLOAT);
}

void LinearlyTransformedCosines::initUniforms() {
    LtcUniforms defaultValues{ .resolution = {width, height} };
    
    uniforms.gpu = device.createCpuVisibleBuffer(&defaultValues, sizeof(defaultValues), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    uniforms.cpu = as<LtcUniforms>(uniforms.gpu.map());
}

void LinearlyTransformedCosines::updateView() {
    glm::mat4 view{1};
    view = glm::translate(view, {0, 6, 0.1 * cam.zoom - 0.5});
    view = glm::rotate(view, glm::radians(cam.rotX - 10.f), {1, 0, 0});
    view = glm::rotate(view, glm::radians(cam.rotY), {0, 1, 0});
    view = glm::scale(view, {1, -1, 1});
    uniforms.cpu->view = view;
}

void LinearlyTransformedCosines::renderControls(VkCommandBuffer commandBuffer) {

    auto& ltc = uniforms.cpu;

    ImGui::Begin("LTC");
    ImGui::SetWindowSize({0, 0});
    ImGui::SliderFloat("Roughness", &ltc->roughness, 0, 1);
    ImGui::ColorEdit3("Diffuse Color", &ltc->dcolor.r);
    ImGui::ColorEdit3("Specular Color", &ltc->scolor.r);
    ImGui::SliderFloat("Light Intensity", &ltc->intensity, 0, 10);
    ImGui::SliderFloat("Width", &ltc->width, 0.1, 15);
    ImGui::SliderFloat("Height", &ltc->height, 0.1, 15);
    ImGui::SliderFloat("Rotation Y", &ltc->roty, 0, 1);
    ImGui::SliderFloat("Rotation Z", &ltc->rotz, 0, 1);

    static bool twoSided = bool(ltc->twoSided);
    ImGui::Checkbox("Two Sided", &twoSided);
    ltc->twoSided = int(twoSided);
    ImGui::End();

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 720;
        settings.height = 720;
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
        auto app = LinearlyTransformedCosines{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}