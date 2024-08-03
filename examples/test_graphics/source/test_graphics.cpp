#include "test_graphics.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "AppContext.hpp"
#include "ExtensionChain.hpp"

test_graphics::test_graphics(const Settings& settings) : VulkanRayTraceBaseApp("test_graphics", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/test_graphics");
    fileManager().addSearchPathFront("../../examples/test_graphics/data");
    fileManager().addSearchPathFront("../../examples/test_graphics/spv");
    fileManager().addSearchPathFront("../../examples/test_graphics/models");
    fileManager().addSearchPathFront("../../examples/test_graphics/textures");
}

void test_graphics::initApp() {
    initCamera();
    createDescriptorPool();
    createPrimitive();
    createInverseCam();
    AppContext::init(device, descriptorPool, swapChain, renderPass);
    loadTexture();
    initCanvas();
    initLoader();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createRenderPipeline();
    createComputePipeline();
    createRayTracingPipeline();
    initFFT();
//
//    runFFT();
//    runFFT();
}

void test_graphics::initFFT() {
    int w, h, c;
    stbi_info(path, &w, &h, &c);

    const auto N = to<int>(std::ceil(std::log2(glm::max(w, h))));
    const auto size = 1 << N;
    fft = FFT(size);
    fft.init();
}

void test_graphics::runFFT() {
    auto w = inverseFD.real.width;
    auto h = inverseFD.real.height;
    auto stagingReal = device.createStagingBuffer(frequencyDomain.real.image.size);
    auto stagingImaginary = device.createStagingBuffer(frequencyDomain.real.image.size);
    auto stagingMagnitude = device.createStagingBuffer(frequencyDomain.real.image.size);

    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        fft(commandBuffer, timeDomain, frequencyDomain);
        fft.inverse(commandBuffer, frequencyDomain, inverseFD);
        textures::copy(commandBuffer, inverseFD.real, renderTTexture);
    });

//    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        fft.inverse(commandBuffer, frequencyDomain, inverseFD);
//    });
//
//
//    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        textures::copy(commandBuffer, inverseFD.real, stagingReal, {w, h});
//        textures::copy(commandBuffer, inverseFD.imaginary, stagingImaginary, {w, h});
//    });
//
//    auto size = w * h;
//    glm::vec4 maxMag{MIN_FLOAT, MIN_FLOAT, MIN_FLOAT, 0};
//
//    auto real = stagingReal.span<glm::vec4>(size);
//    auto imaginary = stagingImaginary.span<glm::vec4>(size);
//    auto magnitudes = stagingMagnitude.span<glm::vec4>(size);
//
//    auto shift = [w, h](int index) {
//        int i = index % w;
//        int j = index / w;
//
//        i = (i + w/2) % w;
//        j = (j + h/2) % h;
//
////        return j * w + i;
//        return index;
//    };
//
//    for(auto i = 0; i < size; ++i) {
//        auto re = real[i];
//        auto im = imaginary[i];
//
////        auto mag = glm::vec4{
////            glm::length(glm::vec2(re.x, im.x)) ,
////            glm::length(glm::vec2(re.y, im.y)) ,
////            glm::length(glm::vec2(re.z, im.z)) ,
////            0
////        };
//        auto mag = re;
//        maxMag = glm::max(maxMag, mag);
//
//        magnitudes[shift(i)] = mag;
//    }
//
//    for(auto& mag : magnitudes) {
////        mag = glm::log(mag + 1.f)/10.f;
//    }
//
//    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
//        textures::transfer(commandBuffer, stagingMagnitude, renderTTexture.image, {w, h}, VK_IMAGE_LAYOUT_GENERAL);
//    });

}



void test_graphics::loadTexture() {
    int w, h, channels;
    auto image = stbi_load(path, &w, &h, &channels, STBI_rgb_alpha);

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



    textures::create(device, timeDomain.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, hrImage.data(), {w, h, 1});

    textures::create(device, timeDomain.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {w, h, 1});
    textures::create(device, frequencyDomain.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {ww, ww, 1});
    textures::create(device, frequencyDomain.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {ww, ww, 1});
    textures::create(device, inverseFD.real, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {w, h, 1});
    textures::create(device, inverseFD.imaginary, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {w, h, 1});
    textures::create(device, renderTTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {w, h, 1});

    timeDomain.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    timeDomain.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    frequencyDomain.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    frequencyDomain.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inverseFD.real.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inverseFD.imaginary.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    renderTTexture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

}

void test_graphics::initCamera() {
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

void test_graphics::initBindlessDescriptor() {
    bindlessDescriptor = plugin<BindLessDescriptorPlugin>(PLUGIN_NAME_BINDLESS_DESCRIPTORS).descriptorSet();
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 0);
    bindlessDescriptor.reserveSlots(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 0);
}

void test_graphics::createPrimitive() {
    constexpr int N = 7;
    constexpr float delta = glm::two_pi<float>()/to<float>(N);
    const auto r = 0.10f;

    std::vector<Vertex> vertices;
    vertices.push_back(Vertex{  .color = glm::vec4(1) });

    for(auto i = 0; i <= N; ++i) {
        auto angle = to<float>(i) * delta;
        glm::vec2 p{ glm::cos(angle), glm::sin(angle) };
        p *= r;
        Vertex v{ .position = glm::vec4(p, 0, 1), .color = glm::vec4(1) };
        vertices.push_back(v);
    }
    primitive.vertices = device.createDeviceLocalBuffer(vertices.data(), BYTE_SIZE(vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void test_graphics::beforeDeviceCreation() {
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

void test_graphics::createDescriptorPool() {
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


void test_graphics::initLoader() {
    loader = std::make_unique<gltf::Loader>(&device, &descriptorPool, &bindlessDescriptor);
    loader->start();
}

void test_graphics::createDescriptorSetLayouts() {
    raytrace.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("ray_trace")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .createLayout();

    textureDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("texture_set_layout")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_FRAGMENT_BIT)
        .createLayout();
}

void test_graphics::updateDescriptorSets(){
    auto sets = descriptorPool.allocate( { raytrace.descriptorSetLayout, textureDescriptorSetLayout });
    raytrace.descriptorSet = sets[0];
    textureDescriptorSet = sets[1];

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
    VkDescriptorBufferInfo camInfo{ inverseCamProj, 0, VK_WHOLE_SIZE};
    writes[1].pBufferInfo = &camInfo;

    writes[2].dstSet = raytrace.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo imageInfo{ VK_NULL_HANDLE, canvas.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &imageInfo;

    writes[3].dstSet = textureDescriptorSet;
    writes[3].dstBinding = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo textureInfo{ renderTTexture.sampler.handle, renderTTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &textureInfo;

    device.updateDescriptorSets(writes);
}

void test_graphics::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void test_graphics::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}

void test_graphics::initCanvas() {
    canvas = Canvas{ this, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_FORMAT_R8G8B8A8_UNORM};
    canvas.init();
}

void test_graphics::createInverseCam() {
    inverseCamProj = device.createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(glm::mat4) * 2);
}

void test_graphics::createRayTracingPipeline() {
    auto rayGenShaderModule = device.createShaderModule( resource("raygen.rgen.spv"));

    auto stages = initializers::rayTraceShaderStages({
                                                             { rayGenShaderModule, VK_SHADER_STAGE_RAYGEN_BIT_KHR}
                                                     });
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups;
    shaderGroups.push_back(shaderTablesDesc.rayGenGroup());

    dispose(raytrace.layout);

    raytrace.layout = device.createPipelineLayout({ raytrace.descriptorSetLayout });
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

void test_graphics::rayTrace(VkCommandBuffer commandBuffer) {
    CanvasToRayTraceBarrier(commandBuffer);

    std::vector<VkDescriptorSet> sets{ raytrace.descriptorSet };
    assert(raytrace.pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.pipeline.handle);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, raytrace.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);

    vkCmdTraceRaysKHR(commandBuffer, bindingTables.rayGen, bindingTables.miss, bindingTables.closestHit,
                      bindingTables.callable, swapChain.extent.width, swapChain.extent.height, 1);

    rayTraceToCanvasBarrier(commandBuffer);
}

void test_graphics::rayTraceToCanvasBarrier(VkCommandBuffer commandBuffer) const {
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

void test_graphics::CanvasToRayTraceBarrier(VkCommandBuffer commandBuffer) const {
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


void test_graphics::createRenderPipeline() {
    //    @formatter:off
        auto builder = prototypes->cloneGraphicsPipeline();
        render.pipeline =
            builder
                .shaderStage()
                    .vertexShader(resource("shaders/flat.vert.spv"))
                    .fragmentShader(resource("shaders/flat.frag.spv"))
                .inputAssemblyState()
                    .triangleFan()
                .rasterizationState()
                    .cullNone()
                .name("render")
                .build(render.layout);
    //    @formatter:on
}

void test_graphics::createComputePipeline() {
    auto module = device.createShaderModule( "../../data/shaders/pass_through.comp.spv");
    auto stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute.layout = device.createPipelineLayout();

    auto computeCreateInfo = initializers::computePipelineCreateInfo();
    computeCreateInfo.stage = stage;
    computeCreateInfo.layout = compute.layout.handle;

    compute.pipeline = device.createComputePipeline(computeCreateInfo, pipelineCache.handle);
}


void test_graphics::onSwapChainDispose() {
    dispose(render.pipeline);
    dispose(compute.pipeline);
    dispose(raytrace.pipeline);
}

void test_graphics::onSwapChainRecreation() {
    initCanvas();
    createRayTracingPipeline();
    updateDescriptorSets();
    createRenderPipeline();
    createComputePipeline();
}

VkCommandBuffer *test_graphics::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
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

    canvas.draw(commandBuffer, textureDescriptorSet);
//    renderPrimitive(commandBuffer);
    vkCmdEndRenderPass(commandBuffer);

//    rayTrace(commandBuffer);


    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void test_graphics::renderPrimitive(VkCommandBuffer commandBuffer) {
    static Camera lCamera{};
    VkDeviceSize  offset = 0;
    const auto vertexCount = primitive.vertices.sizeAs<Vertex>();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, render.pipeline.handle);
    vkCmdPushConstants(commandBuffer, render.layout.handle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Camera), &lCamera);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, primitive.vertices, &offset);
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void test_graphics::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
    inverseCamProj.map<glm::mat4>([&](auto ptr){
        auto view = glm::inverse(cam.view);
        auto proj = glm::inverse(cam.proj);
        *ptr = view;
        *(ptr+1) = proj;
    });
}

void test_graphics::checkAppInputs() {
    camera->processInput();
}

void test_graphics::cleanup() {
    loader->stop();
    AppContext::shutdown();
}

void test_graphics::onPause() {
    VulkanBaseApp::onPause();
}

void test_graphics::endFrame() {
    static bool once = true;
    static int count = 0;
    if(once & count > 100) {
        textures::save(device, "heptagon.jpg", colorBuffer.width, colorBuffer.height, colorBuffer.format, colorBuffer.image, FileFormat::JPG);
        once = false;
        spdlog::info("heptagon saved");
    }
    ++count;
    runFFT();
}


int main(){
    try{

        Settings settings;
        settings.width = 512;
        settings.height = 512;
        settings.depthTest = true;
        settings.enabledFeatures.wideLines = true;
        settings.enableBindlessDescriptors = true;
        settings.deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        settings.deviceExtensions.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME);
        settings.uniqueQueueFlags = VK_QUEUE_TRANSFER_BIT;
        settings.enabledFeatures.fillModeNonSolid = VK_TRUE;
        settings.enabledFeatures.multiDrawIndirect = VK_TRUE;
        settings.msaaSamples = VK_SAMPLE_COUNT_8_BIT;

        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        auto app = test_graphics{ settings };
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}