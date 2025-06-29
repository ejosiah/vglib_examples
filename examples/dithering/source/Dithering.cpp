#include "Dithering.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"
#include "Barrier.hpp"

Dithering::Dithering(const Settings& settings) : VulkanBaseApp("Dithering", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../dependencies/glTF-Sample-Assets/Models");
    fileManager().addSearchPathFront("../data");
    fileManager().addSearchPathFront("../data/textures");
    fileManager().addSearchPathFront("../data/shaders");
    fileManager().addSearchPathFront("../data/models");
    fileManager().addSearchPathFront("dithering");
    fileManager().addSearchPathFront("dithering/data");
    fileManager().addSearchPathFront("dithering/spv");
    fileManager().addSearchPathFront("dithering/models");
    fileManager().addSearchPathFront("dithering/textures");
}

void Dithering::initApp() {
    constants.viewportSize = { width * 0.75f, height };
    initCamera();
    createDescriptorPool();
    initBindlessDescriptor();
    createBayerMatrix();
    loadTextures();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    compute = std::make_unique<ComputePipelines>(&device, pipelineMetaData());
    compute->createPipelines();
}

void Dithering::initCamera() {
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

void Dithering::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6);
}

void Dithering::beforeDeviceCreation() {
    auto dsFeatures = findExtension<VkPhysicalDeviceExtendedDynamicState3FeaturesEXT>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT, deviceCreateNextChain);
    dsFeatures->extendedDynamicState3PolygonMode = VK_TRUE;

    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    devFeatures13->synchronization2 = VK_TRUE;
    devFeatures13->dynamicRendering = VK_TRUE;
    devFeatures13->maintenance4 = VK_TRUE;
}

void Dithering::createDescriptorPool() {
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


void Dithering::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void Dithering::createDescriptorSetLayouts() {
    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();

    bayerMatrixBufferDescriptorSet =
        device.descriptorSetLayoutBuilder()
            .name("bayer_matrix_descriptor_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();
}

void Dithering::updateDescriptorSets(){
    auto sets = descriptorPool.allocate({ textureDescriptorSetLayout, textureDescriptorSetLayout, textureDescriptorSetLayout });
    gradientDescriptorSet = sets[0];
    pictureDescriptorSet = sets[1];
    descriptorSet = sets[2];
    auto writes = initializers::writeDescriptorSets<3>();
    
    writes[0].dstSet = gradientDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorImageInfo gradientInfo{ gradient.sampler.handle, gradient.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[0].pImageInfo = &gradientInfo;

    writes[1].dstSet = pictureDescriptorSet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorImageInfo pictureInfo{ picture.sampler.handle, picture.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    writes[1].pImageInfo = &pictureInfo;

    writes[2].dstSet = descriptorSet;
    writes[2].dstBinding = 0;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    VkDescriptorImageInfo ditherInfo{ ditheredImage.sampler.handle, ditheredImage.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    writes[2].pImageInfo = &ditherInfo;

    device.updateDescriptorSets(writes);

    bayerMatrixDescriptorSet = descriptorPool.allocateN(bayerMatrixBufferDescriptorSet, bayerMatrixSet.size());
    writes.resize(bayerMatrixSet.size());

    auto matrixInfo = map_range(bayerMatrixSet, [](auto matrix){ return VkDescriptorBufferInfo{ matrix.buffer, 0, VK_WHOLE_SIZE }; });
    for(auto i = 0; i < matrixInfo.size(); ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = bayerMatrixDescriptorSet[i];
        writes[i].dstBinding = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &matrixInfo[i];
    }

    device.updateDescriptorSets(writes);

}

void Dithering::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void Dithering::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void Dithering::createRenderPipeline() {
    //    @formatter:off
        render.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("quad.vert.spv"))
                    .fragmentShader(resource("quad.frag.spv"))
                .viewportState().clear()
                    .viewport()
                        .origin(to<float>(width/4u), 0)
                        .dimension(to<uint32_t>(width * 3)/4, height)
                        .minDepth(0)
                        .maxDepth(1)
                    .scissor()
                        .offset(width/4, 0)
                        .extent((width * 3)/4, height)
                    .add()
                .layout()
                    .addDescriptorSetLayout(textureDescriptorSetLayout)
                .name("render")
                .build(render.layout);
    //    @formatter:on
}


void Dithering::onSwapChainDispose() {
    dispose(render.pipeline);
}

void Dithering::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
}

VkCommandBuffer *Dithering::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    clearColor(0, 0, 1);

    renderToSwapChain([&]{

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
        AppContext::renderClipSpaceQuad(commandBuffer);

        renderUI(commandBuffer);
    }, commandBuffer);

    dither(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void Dithering::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void Dithering::checkAppInputs() {
    camera->processInput();
}

void Dithering::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void Dithering::onPause() {
    VulkanBaseApp::onPause();
}

void Dithering::renderUI(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Options", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::SetWindowSize({to<float>(width) * .25f, to<float>(height)});
    ImGui::SetWindowPos({0, 0});

    ImGui::Combo("Method", &method, methods.data(), NumMethods);
    ImGui::Combo("Target", &target, targets.data(), NumTargets);

    if(method == Ordered) {
        ImGui::Combo("Matrix", &bayerMatrixIndex, matrixLabel.data(), NumBayerMatrix);
    }

    bool bGrayScale = to<bool>(constants.grayScale);
    ImGui::Checkbox("Gray scale", &bGrayScale);

    bool bGammaCorrect = to<bool>(constants.gammaCorrect);
    ImGui::Checkbox("Gamma correct", &bGammaCorrect);

    ImGui::Button("Save");

    ImGui::End();

    constants.grayScale = to<int>(bGrayScale);
    constants.gammaCorrect = to<int>(bGammaCorrect);
    constants.blockSize = 1 << (bayerMatrixIndex + 1);

    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void Dithering::loadTextures() {
    auto gradientGen = [](auto i, auto j, auto w, auto h) {
        return glm::vec4{to<float>(i)/to<float>(w)};
    };
    auto whiteNoiseGen = [rng=rng(0.f, 1.f)] (auto i, auto j, auto w, auto h) mutable {
        return glm::vec4(rng(), rng(), rng(), rng());
    };

    whiteNoise.sampler = createNoiseSampler();

    textures::fromFile(device, picture, resource("pexels-lestrade84-17811.jpg"), false, VK_FORMAT_R8G8B8A8_SRGB, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textures::generate(device, gradient, picture.width, picture.height, gradientGen);
    textures::fromFile(device, blueNoise, resource("BlueNoiseTextures/128_128/LDR_RGBA_0.png"), false, VK_FORMAT_R8G8B8A8_UNORM);
    textures::generate(device, whiteNoise, 128, 128, whiteNoiseGen);
    textures::color(device, whiteTexture, glm::vec3{1}, {128, 128});
    textures::createNoTransition(device, ditheredImage, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {picture.width, picture.height, 1});
    source = &gradient;

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        Barriers::push(ditheredImage.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::flush(commandBuffer);
    });

    bindlessDescriptor.update({ &picture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TextureBindingId::SourceImage });
    bindlessDescriptor.update({ &whiteNoise, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TextureBindingId::Noise });
    bindlessDescriptor.update({ &ditheredImage, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ImageBindingId::DitheredImage, VK_IMAGE_LAYOUT_GENERAL });
}

std::vector<PipelineMetaData> Dithering::pipelineMetaData() {
    return {
        {
            .name = "noise_dither",
            .shadePath = resource("noise_dither.comp.spv"),
            .layouts = { const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout) },
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
        },
        {
            .name = "ordered_dither",
            .shadePath = resource("ordered_dither.comp.spv"),
            .layouts = { const_cast<VulkanDescriptorSetLayout*>(bindlessDescriptor.descriptorSetLayout), &bayerMatrixBufferDescriptorSet },
            .ranges = { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} }
        },
    };
}

VulkanSampler Dithering::createNoiseSampler() {
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

    return device.createSampler(samplerInfo);
}

void Dithering::dither(VkCommandBuffer commandBuffer) {
    const auto gx = (source->width + 7)/8;
    const auto gy = (source->height + 7)/8;

    Barrier::fragmentReadToComputeWrite(commandBuffer);

    if(method == Ordered) {
        orderedDither(commandBuffer, gx, gy);
    }else {
        noiseDither(commandBuffer, gx, gy);
    }

    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void Dithering::endFrame() {
    static auto previousTarget = target;
    if(previousTarget != target) {
        source = (target == Gradient) ? &gradient : &picture;
        bindlessDescriptor.update({source, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TextureBindingId::SourceImage});
        previousTarget = target;
    }

    if(method == None || method == WhiteNoise || method == BlueNoise) {
        static auto prevMethod = method;
        if(prevMethod != method) {
            Texture* noiseTexture = method == None ? &whiteTexture : (method == WhiteNoise ? &whiteNoise : &blueNoise);
            bindlessDescriptor.update({ noiseTexture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, TextureBindingId::Noise });
            prevMethod = method;
        }

    }
}

void generateBayerMatrixFlat(std::vector<int>& matrix, int x, int y, int size, int value, int step, int dim) {
    if (size == 1) {
        matrix[y * dim + x] = value;
        return;
    }

    int half = size / 2;

    generateBayerMatrixFlat(matrix, x, y,               half, value + step * 0, step * 4, dim);
    generateBayerMatrixFlat(matrix, x + half, y,        half, value + step * 2, step * 4, dim);
    generateBayerMatrixFlat(matrix, x, y + half,        half, value + step * 3, step * 4, dim);
    generateBayerMatrixFlat(matrix, x + half, y + half, half, value + step * 1, step * 4, dim);
}

void Dithering::createBayerMatrix() {

    for(auto i = 0; i < NumBayerMatrix; ++i) {
        int dim = 1 << (i+1); // Matrix size = 2^n
        std::vector<int> matrix(dim * dim);
        generateBayerMatrixFlat(matrix, 0, 0, dim, 0, 1, dim);
        VulkanBuffer buffer = device.createDeviceLocalBuffer(matrix.data(), BYTE_SIZE(matrix), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        bayerMatrixSet.push_back(std::move(buffer));
    }

}

void Dithering::noiseDither(VkCommandBuffer commandBuffer, uint32_t gx, uint32_t gy) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline("noise_dither"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout("noise_dither"),
                            0, 1, &bindlessDescriptor.descriptorSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute->layout("noise_dither"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);
}

void Dithering::orderedDither(VkCommandBuffer commandBuffer, uint32_t gx, uint32_t gy) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = bindlessDescriptor.descriptorSet;
    sets[1] = bayerMatrixDescriptorSet[bayerMatrixIndex];

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->pipeline("ordered_dither"));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute->layout("ordered_dither"),
                            0, COUNT(sets), sets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, compute->layout("ordered_dither"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, gx, gy, 1);

}


int main(){
    try{
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 900;
        settings.height = 1080;
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
        auto app = Dithering{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}