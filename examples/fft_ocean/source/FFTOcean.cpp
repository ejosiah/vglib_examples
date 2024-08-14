#include "FFTOcean.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

#include <random>

FFTOcean::FFTOcean(const Settings& settings)
: VulkanBaseApp("FFT Ocean", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/fft_ocean");
    fileManager().addSearchPathFront("../../examples/fft_ocean/data");
    fileManager().addSearchPathFront("../../examples/fft_ocean/spv");
    fileManager().addSearchPathFront("../../examples/fft_ocean/models");
    fileManager().addSearchPathFront("../../examples/fft_ocean/textures");
}

void FFTOcean::initApp() {
    debugAction = &mapToMouse(static_cast<int>(MouseEvent::Button::RIGHT), "next texture", Action::detectInitialPressOnly());
    initScreenQuad();
    initBindlessDescriptor();
    initCamera();
    createPatch();
    createDescriptorPool();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    initLoader();
    initCanvas();
    initTextures();
    loadInputSignal();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    initFFT();

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        createHeightField(commandBuffer);
    });
}

void FFTOcean::initCanvas() {
    canvas = Canvas{ this, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    canvas.init();
}

void FFTOcean::initFFT() {
    fft = FFT{hSize};
    fft.init();
}

std::vector<glm::vec4> FFTOcean::createDistribution(float mu, float sigma) {
    constexpr auto two_pi = glm::two_pi<float>();

    //initialize the random uniform number generator (runif) in a range 0 to 1
    std::mt19937 rng(std::random_device{}()); // Standard mersenne_twister_engine seeded with rd()
    std::uniform_real_distribution<> runif(0.0, 1.0);
    auto generateGaussianNoise = [&]{
        double u1, u2;
        do
        {
            u1 = runif(rng);
        }
        while (u1 == 0);
        u2 = runif(rng);

        //compute z0 and z1
        auto mag = sigma * sqrt(-2.0 * log(u1));
        auto z0  = mag * cos(two_pi * u2) + mu;
        auto z1  = mag * sin(two_pi * u2) + mu;

        return glm::vec2{z0, z1};
    };

    const auto N = hSize * hSize;

    std::vector<glm::vec4> dist;
    dist.reserve(N);
    for(auto i = 0; i < N; ++i){
        auto a = generateGaussianNoise();
        auto b = generateGaussianNoise();

        dist.emplace_back(a, b);
    }

    return dist;
}

void FFTOcean::initTextures() {
    const auto N = hSize;

    auto dist = createDistribution();
//    textures::createNoTransition(device, distribution, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    textures::create(device, distribution, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, dist.data(), {N, N, 1});


    textures::createNoTransition(device, tilde_h0k, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    textures::createNoTransition(device, tilde_h0_minus_k, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});

    textures::createNoTransition(device, hkt.signal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    textures::createNoTransition(device, hkt.signal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});

    textures::createNoTransition(device, heightFieldSpectral.signal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    textures::createNoTransition(device, heightFieldSpectral.signal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    textures::createNoTransition(device, heightField.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});


    distribution.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    tilde_h0k.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    tilde_h0_minus_k.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    hkt.signal.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    hkt.signal.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    heightField.texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    distribution.bindingId = 0;
    tilde_h0k.bindingId = 1;
    tilde_h0_minus_k.bindingId = 2;
    hkt.signal.real.bindingId = 3;
    hkt.signal.imaginary.bindingId = 4;
    heightFieldSpectral.signal.real.bindingId = 5;
    heightFieldSpectral.signal.imaginary.bindingId = 6;
    heightField.texture.bindingId = 7;

    bindlessDescriptor.update(distribution, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(tilde_h0k, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(tilde_h0_minus_k, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(hkt.signal.real, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(hkt.signal.imaginary, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(heightFieldSpectral.signal.real, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(heightFieldSpectral.signal.imaginary, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    bindlessDescriptor.update(heightField.texture, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
}

void FFTOcean::loadInputSignal() {
    int w, h, channels;
    auto image = stbi_load(R"(C:\Users\Josiah Ebhomenye\CLionProjects\vglib_examples\examples\signal_processing\textures\lena_1024.png)", &w, &h, &channels, STBI_rgb_alpha);

    std::vector<float> hrImage(w * h *  4, 0);
    for(int i = 0; i < w; ++i){
        if(i >= w) continue;

        for(int j = 0; j < h; ++j){
            if(j >= h) continue;

            int mid = i * h + j;
            int id = i * h + j;
            glm::vec4 color{
                    image[mid * 4 + 0]/255.f,
                    image[mid * 4 + 1]/255.f,
                    image[mid * 4 + 2]/255.f,
                    image[mid * 4 + 3]/255.f,
            };
            hrImage[id * 4 + 0] = color.r;
            hrImage[id * 4 + 1] = color.g;
            hrImage[id * 4 + 2] = color.b;
            hrImage[id * 4 + 3] = color.a;
        }
    }
    stbi_image_free(image);

    const auto N = to<int>(std::ceil(std::log2(glm::max(w, h))));
    const auto ww = 1 << N;

    textures::create(device, inputSignal.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, hrImage.data(), {w, h, 1});
    textures::createNoTransition(device, inputSignal.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {ww, ww, 1});

    inputSignal.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inputSignal.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
}

void FFTOcean::initCamera() {
    FirstPersonSpectatorCameraSettings cameraSettings;
    cameraSettings.fieldOfView = 60.0f;
    cameraSettings.aspectRatio = float(swapChain.extent.width)/float(swapChain.extent.height);
    cameraSettings.velocity = glm::vec3(100);
    cameraSettings.acceleration = glm::vec3(50);

    camera = std::make_unique<FirstPersonCameraController>(dynamic_cast<InputManager&>(*this), cameraSettings);
    camera->lookAt({500, 100, 0}, glm::vec3(0), {0, 1, 0});
}

void FFTOcean::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void FFTOcean::beforeDeviceCreation() {
    auto devFeatures13 = findExtension<VkPhysicalDeviceVulkan13Features>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, deviceCreateNextChain);
    if(devFeatures13.has_value()) {
        devFeatures13.value()->synchronization2 = VK_TRUE;
        devFeatures13.value()->dynamicRendering = VK_TRUE;
    }else {
        static VkPhysicalDeviceVulkan13Features devFeatures13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        devFeatures13.synchronization2 = VK_TRUE;
        devFeatures13.dynamicRendering = VK_TRUE;
        deviceCreateNextChain = addExtension(deviceCreateNextChain, devFeatures13);
    };

    static VkPhysicalDeviceExtendedDynamicState3FeaturesEXT dsFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT };
    dsFeatures.extendedDynamicState3PolygonMode = VK_TRUE;
    deviceCreateNextChain = addExtension(deviceCreateNextChain, dsFeatures);
}

void FFTOcean::createDescriptorPool() {
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


void FFTOcean::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void FFTOcean::createDescriptorSetLayouts() {
    imageDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("gaussian_distribution")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();

    complexSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("complex_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .createLayout();
}

void FFTOcean::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( {
        imageDescriptorSetLayout, imageDescriptorSetLayout, imageDescriptorSetLayout,
        imageDescriptorSetLayout, complexSetLayout, complexSetLayout  });

    descriptorSets.distributionImageSet = sets[0];
    descriptorSets.tilde_h0k = sets[1];
    descriptorSets.tilde_h0_minus_k = sets[2];
    heightField.descriptorSet = sets[3];
    hkt.descriptorSet = sets[4];
    heightFieldSpectral.descriptorSet = sets[5];

    auto writes = initializers::writeDescriptorSets<8>();
    
    writes[0].dstSet = descriptorSets.distributionImageSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo distributionImageInfo{VK_NULL_HANDLE, distribution.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &distributionImageInfo;

    writes[1].dstSet = descriptorSets.tilde_h0k;
    writes[1].dstBinding = 0;
    writes[1].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo hk0_ImageInfo{VK_NULL_HANDLE, tilde_h0k.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &hk0_ImageInfo;

    writes[2].dstSet = descriptorSets.tilde_h0_minus_k;
    writes[2].dstBinding = 0;
    writes[2].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo h_minus_k0_ImageInfo{VK_NULL_HANDLE, tilde_h0_minus_k.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &h_minus_k0_ImageInfo;

    writes[3].dstSet = hkt.descriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo hkt_dy_realInfo{VK_NULL_HANDLE, hkt.signal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &hkt_dy_realInfo;

    writes[4].dstSet = hkt.descriptorSet;
    writes[4].dstBinding = 1;
    writes[4].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo hkt_dy_imagInfo{VK_NULL_HANDLE, hkt.signal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[4].pImageInfo = &hkt_dy_imagInfo;

    writes[5].dstSet = heightField.descriptorSet;
    writes[5].dstBinding = 0;
    writes[5].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo heightFieldImagInfo{VK_NULL_HANDLE, heightField.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[5].pImageInfo = &heightFieldImagInfo;

    writes[6].dstSet = heightFieldSpectral.descriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo hf_realInfo{VK_NULL_HANDLE, heightFieldSpectral.signal.real.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[6].pImageInfo = &hf_realInfo;

    writes[7].dstSet = heightFieldSpectral.descriptorSet;
    writes[7].dstBinding = 1;
    writes[7].descriptorType =  VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = 1;
    VkDescriptorImageInfo hf_imagInfo{VK_NULL_HANDLE, heightFieldSpectral.signal.imaginary.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[7].pImageInfo = &hf_imagInfo;

    device.updateDescriptorSets(writes);
}

void FFTOcean::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void FFTOcean::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void FFTOcean::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.ocean.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("ocean.vert.spv"))
                    .tessellationControlShader(resource("ocean.tesc.spv"))
                    .tessellationEvaluationShader(resource("ocean.tese.spv"))
                    .fragmentShader(resource("ocean.frag.spv"))
                .vertexInputState().clear()
                    .addVertexBindingDescription(0, sizeof(glm::vec4), VK_VERTEX_INPUT_RATE_VERTEX)
                    .addVertexAttributeDescription(0, 0, VK_FORMAT_R32G32_SFLOAT, 0)
                    .addVertexAttributeDescription(1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(glm::vec2))
                .inputAssemblyState()
                    .patches()
                .tessellationState()
                    .patchControlPoints(4)
                    .domainOrigin(VK_TESSELLATION_DOMAIN_ORIGIN_LOWER_LEFT)
                .rasterizationState()
                    .cullNone()
                    .polygonModeLine()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 0, sizeof(renderConstants))
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("ocean_render")
                .build(render.ocean.layout);

        render.debug.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("screen_quad.vert.spv"))
                    .fragmentShader(resource("screen_quad.frag.spv"))
                .layout()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float))
                    .addDescriptorSetLayout(*bindlessDescriptor.descriptorSetLayout)
                .name("ocean_fft_debug")
            .build(render.debug.layout);
    //    @formatter:on
}

void FFTOcean::createComputePipeline() {
    auto module = device.createShaderModule(resource("gaussian_noise_distribution.comp.spv"));
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.distribution.layout = device.createPipelineLayout({ imageDescriptorSetLayout });

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.distribution.layout.handle;

    compute.distribution.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("gaussian_noise_distribution", compute.distribution.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("gaussian_noise_distribution", compute.distribution.layout.handle);

    // Generate Spectral components
    module = device.createShaderModule(resource("generate_spectral_components.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.spectral_components.layout =
            device.createPipelineLayout(
                    { imageDescriptorSetLayout, imageDescriptorSetLayout, imageDescriptorSetLayout },
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.spectral_components.layout.handle;
    compute.spectral_components.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_spectral_components", compute.spectral_components.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("generate_spectral_components", compute.spectral_components.layout.handle);

    // Generate Spectral height field
    module = device.createShaderModule(resource("generate_spectral_height_field.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.spectral_height_field.layout =
            device.createPipelineLayout(
                    { imageDescriptorSetLayout, imageDescriptorSetLayout, complexSetLayout },
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.spectral_height_field.layout.handle;
    compute.spectral_height_field.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_spectral_height_field", compute.spectral_height_field.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("generate_spectral_height_field", compute.spectral_height_field.layout.handle);

    // Generate  eight field magnitude
    module = device.createShaderModule(resource("generate_height_field_mag.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    compute.height_field_mag.layout =
            device.createPipelineLayout(
                    { complexSetLayout, imageDescriptorSetLayout },
                    { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants)} });

    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.height_field_mag.layout.handle;
    compute.height_field_mag.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE>("generate_height_field_mag", compute.height_field_mag.pipeline.handle);
    device.setName<VK_OBJECT_TYPE_PIPELINE_LAYOUT>("generate_height_field_mag", compute.height_field_mag.layout.handle);
}


void FFTOcean::onSwapChainDispose() {
    dispose(render.ocean.pipeline);
    dispose(render.debug.pipeline);
    dispose(compute.distribution.pipeline);
}

void FFTOcean::onSwapChainRecreation() {
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *FFTOcean::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 0, 1};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    renderOcean(commandBuffer);
//    renderScreenQuad(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    createHeightField(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void FFTOcean::renderOcean(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;

    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = bindlessDescriptor.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ocean.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.ocean.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdPushConstants(commandBuffer, render.ocean.layout.handle, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, 0, sizeof(renderConstants), &renderConstants);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, patch, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}

void FFTOcean::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    constants.time += time;
    renderConstants.camera = cam;
    renderConstants.time += time;
}

void FFTOcean::checkAppInputs() {
    camera->processInput();
    if(debugAction->isPressed()) {
        render.debug.action++;
        render.debug.action %= 5;
    }
}

void FFTOcean::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void FFTOcean::onPause() {
    VulkanBaseApp::onPause();
}

void FFTOcean::createHeightField(VkCommandBuffer commandBuffer) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;

//    createDistribution(commandBuffer);
//
//    barrier.image = distribution.image;
//    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    createSpectralComponents(commandBuffer);

    barrier.image = tilde_h0k.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.image = tilde_h0_minus_k.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    createSpectralHeightField(commandBuffer);

//    fft(commandBuffer, inputSignal, hkt.signal);

    barrier.image = hkt.signal.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
    barrier.image = hkt.signal.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    fft.inverse(commandBuffer, hkt.signal, heightFieldSpectral.signal);

    barrier.image = heightFieldSpectral.signal.real.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
    barrier.image = heightFieldSpectral.signal.imaginary.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    extractHeightFieldMagnitude(commandBuffer);

    barrier.image = heightField.texture.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);
}

void FFTOcean::createDistribution(VkCommandBuffer commandBuffer) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.distribution.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.distribution.layout.handle, 0, 1, &descriptorSets.distributionImageSet, 0, 0);
    vkCmdDispatch(commandBuffer, hSize/32, hSize/32, 1);

}

void FFTOcean::createSpectralComponents(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = descriptorSets.distributionImageSet;
    sets[1] = descriptorSets.tilde_h0k;
    sets[2] = descriptorSets.tilde_h0_minus_k;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.spectral_components.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.spectral_components.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdPushConstants(commandBuffer, compute.spectral_components.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, hSize/32, hSize/32, 1);

}

void FFTOcean::createSpectralHeightField(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = descriptorSets.tilde_h0k;
    sets[1] = descriptorSets.tilde_h0_minus_k;
    sets[2] = hkt.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.spectral_height_field.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.spectral_height_field.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdPushConstants(commandBuffer, compute.spectral_height_field.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
    vkCmdDispatch(commandBuffer, hSize/32, hSize/32, 1);
}

void FFTOcean::extractHeightFieldMagnitude(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = heightFieldSpectral.descriptorSet;
    sets[1] = heightField.descriptorSet;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.height_field_mag.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.height_field_mag.layout.handle, 0, sets.size(), sets.data(), 0, 0);
    vkCmdDispatch(commandBuffer, hSize/32, hSize/32, 1);
}

void FFTOcean::copyToCanvas(VkCommandBuffer commandBuffer, const VulkanImage &source) {
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.subresourceRange = DEFAULT_SUB_RANGE;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = source.image;

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = canvas.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0};
    region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0};
    region.extent = { hSize, hSize, 1 };

    vkCmdCopyImage(commandBuffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, canvas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = source.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.image = canvas.image;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,  0, 0, 0, 0, 0, 1, &barrier);

}

void FFTOcean::createPatch() {
    const auto L = constants.horizontalLength + 24;
    std::vector<glm::vec4> patchPoints{ {0, 0, 0, 0}, {L, 0, 1, 0}, {L, L, 1, 1}, {0, L, 0, 1}};
    patch = device.createDeviceLocalBuffer(patchPoints.data(), BYTE_SIZE(patchPoints), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void FFTOcean::initScreenQuad() {
    auto vertices = ClipSpace::Quad::positions;
    screenQuad  = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void FFTOcean::renderScreenQuad(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 1> sets;
    sets[0] = bindlessDescriptor.descriptorSet;
    VkDeviceSize offset = 0;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.debug.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.debug.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, render.debug.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(int), &render.debug.action);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, screenQuad, &offset);
    vkCmdDraw(commandBuffer, 4, 1, 0, 0);
}


int main(){
    try{

        Settings settings;
        settings.width =  1024;
        settings.height =  1024;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;
        settings.enabledFeatures.tessellationShader = VK_TRUE;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = FFTOcean{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}