#include "SignalProcessing.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "DescriptorSetBuilder.hpp"
#include "ImGuiPlugin.hpp"
#include "implot.h"
#include "dft.hpp"
#include "vulkan_image_ops.h"

SignalProcessing::SignalProcessing(const Settings& settings) : VulkanBaseApp("signal processing", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("../../examples/signal_processing");
    fileManager().addSearchPathFront("../../examples/signal_processing/data");
    fileManager().addSearchPathFront("../../examples/signal_processing/spv");
    fileManager().addSearchPathFront("../../examples/signal_processing/models");
    fileManager().addSearchPathFront("../../examples/signal_processing/textures");

    syncFeatures = VkPhysicalDeviceSynchronization2Features{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
            VK_NULL_HANDLE,
            VK_TRUE
    };
    deviceCreateNextChain = &syncFeatures;
}

void SignalProcessing::initApp() {
    initCamera();
    createBuffers();
    createButterflyLookup();
    initData();
    loadImageSignal();
    createDescriptorPool();
    createDescriptorSetLayouts();
    updateDescriptorSets();
    createCommandPool();
    createPipelineCache();
    createComputeFFTPipeline();

    run2DFFT();
}

void SignalProcessing::initCamera() {
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

void SignalProcessing::createBuffers() {
    VkDeviceSize size = N * sizeof(float);
    boxFilter.realBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    boxFilter.imaginaryBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    boxFilter.real = reinterpret_cast<float*>(boxFilter.realBuffer.map());
    boxFilter.imaginary = reinterpret_cast<float*>(boxFilter.imaginaryBuffer.map());

    SincFilter.realBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    SincFilter.imaginaryBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, size);
    SincFilter.real = reinterpret_cast<float*>(SincFilter.realBuffer.map());
    SincFilter.imaginary = reinterpret_cast<float*>(SincFilter.imaginaryBuffer.map());

    fft_prep_render.maxMagnitudeBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY, sizeof(int));
}

void SignalProcessing::createButterflyLookup() {
    auto nButterflies = static_cast<int>(std::log2(N));
    std::vector<std::complex<double>> butterflyLut(N * nButterflies);
    std::vector<int> indexes(N * nButterflies * 2);
    createButterflyLookups(indexes, butterflyLut, nButterflies);

    std::vector<glm::vec2> butterflyLutVec2{};
    for(const auto& c : butterflyLut){
        butterflyLutVec2.emplace_back(c.real(), c.imag());
    }

    textures::create(device, fftData.butterfly.index, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SINT, indexes.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(int));
    textures::create(device, fftData.butterfly.lut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, butterflyLutVec2.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));

    createButterflyLookups(indexes, butterflyLut, nButterflies, true);
    butterflyLutVec2.clear();
    for(const auto& c : butterflyLut){
        butterflyLutVec2.emplace_back(c.real(), c.imag());
    }

    textures::create(device, inverseFFTData.butterfly.index, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SINT, indexes.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(int));
    textures::create(device, inverseFFTData.butterfly.lut, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32_SFLOAT, butterflyLutVec2.data(), {N, nButterflies, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, sizeof(float));
}

void SignalProcessing::initData() {
    std::vector<float> data;
    std::vector<float> imaginary(N);
    float dt = (1.0f/static_cast<float>(N));
    float t = 0;
    float freq = 50;
    for(int i = 0; i < N; i++){
        float x = t;
        auto y = 0.f;
        y += glm::cos(glm::two_pi<float>() * 2 * x);
        y += glm::cos(glm::two_pi<float>() * 5 * x);
        y += glm::cos(glm::two_pi<float>() * 10 * x);
        y += glm::cos(glm::two_pi<float>() * 20 * x);
        y += glm::cos(glm::two_pi<float>() * 250 * x);
        signal.xData.push_back(x);
        data.push_back(y);

        x = 2 * x - 1;
        boxFilter.real[i] = glm::step(abs(x), 0.5f);
        boxFilter.imaginary[i] = 0;
        boxFilter.xData.push_back(x);

        float num = glm::sin(glm::pi<float>() * x * freq);
        float denum = glm::pi<float>() * x * freq;

        SincFilter.real[i] = denum == 0 ? 1 : num/denum;
//        if(i%2 != 0) SincFilter.real[i] *= -1;
        SincFilter.imaginary[i] = 0;
        SincFilter.xData.push_back(x);

        t += dt;
    }

    signal.realBuffer = device.createCpuVisibleBuffer(data.data(), BYTE_SIZE(data), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    signal.imaginaryBuffer = device.createCpuVisibleBuffer(imaginary.data(), BYTE_SIZE(imaginary), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

    signal.real = reinterpret_cast<float*>(signal.realBuffer.map());
    signal.imaginary = reinterpret_cast<float*>(signal.imaginaryBuffer.map());

    frequency.realBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, BYTE_SIZE(data));
    frequency.imaginaryBuffer = device.createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, BYTE_SIZE(data));
    frequency.real = reinterpret_cast<float*>(frequency.realBuffer.map());
    frequency.imaginary = reinterpret_cast<float*>(frequency.imaginaryBuffer.map());
    frequency.xData = std::vector<float>(N);
    std::iota(frequency.xData.begin(), frequency.xData.end(), 0);

    computeFFT();

}

void SignalProcessing::computeFFT() {
    std::vector<std::complex<double>> data;
    for(int i = 0; i < N; i++){
        std::complex<double> c{signal.real[i], 0};
//        std::complex<double> c{SincFilter.real[i], 0};
        data.push_back(c);
    }
    auto freq_space_values = fft(data);

    for(int i = 0; i < N; i++){
        const auto& c = freq_space_values[i];
        frequency.xData.push_back(static_cast<float>(i));
        frequency.real[i] = static_cast<float>(c.real());
        frequency.imaginary[i] = static_cast<float>(c.imag());
//        frequency.real[i] = static_cast<float>(std::abs(c));
//        frequency.imaginary[i] = static_cast<float>(std::arg(c));
        spdlog::info("{}", frequency.real[i]);
    }
}

void SignalProcessing::run2DFFT() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        maskTexture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE,
                                           VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        clearImages(commandBuffer);
        computeMask(commandBuffer);
        computeLuminance(commandBuffer);
        addImageMemoryBarriers(commandBuffer, { fftData.signal_real[0].image});
        compute2DFFT(commandBuffer);
        addImageMemoryBarriers(commandBuffer, { fftData.signal_real[0].image, fftData.signal_imaginary[0].image});
        applyMask(commandBuffer);
        addImageMemoryBarriers(commandBuffer, { fftData.signal_real[0].image, fftData.signal_imaginary[0].image});
        prepFFTForRender(commandBuffer);

        compute2DInverseFFT(commandBuffer);
        addImageMemoryBarriers(commandBuffer, { inverseFFTData.signal_real[0].image, inverseFFTData.signal_imaginary[0].image});
        prepInverseFFTForRender(commandBuffer);

        maskTexture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                           DEFAULT_SUB_RANGE, VK_ACCESS_SHADER_WRITE_BIT,
                                           VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    });
}

void SignalProcessing::clearImages(VkCommandBuffer commandBuffer) {
    VkClearColorValue clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    vkCmdClearColorImage(commandBuffer, fftData.signal_real[0].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, fftData.signal_imaginary[0].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, fftData.signal_real[1].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, fftData.signal_imaginary[1].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);

    vkCmdClearColorImage(commandBuffer, inverseFFTData.signal_real[0].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, inverseFFTData.signal_imaginary[0].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, inverseFFTData.signal_real[1].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
    vkCmdClearColorImage(commandBuffer, inverseFFTData.signal_imaginary[1].image, VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &DEFAULT_SUB_RANGE);
}

void SignalProcessing::computeFFTGPU() {
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer){
        auto passes = static_cast<int>(std::log2(N));


        static std::array<VkDescriptorSet, 3> sets;
        sets[0] = fftData.signalDescriptorSets[0];
        sets[1] = fftData.signalDescriptorSets[1];
        sets[2] = fftData.lookupDescriptorSet;
        for(auto pass = 0; pass < passes; ++pass){
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.pipeline_vertical.handle);
            vkCmdPushConstants(commandBuffer, compute_fft.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
            vkCmdDispatch(commandBuffer, 1, N, 1);

            if(pass < passes - 1){
                auto output = 1 - (pass % 2);
                addImageMemoryBarriers(commandBuffer, { fftData.signal_real[output].image, fftData.signal_imaginary[output].image});
                std::swap(sets[0], sets[1]);
            }
        }
        addMemoryBarrier(commandBuffer);
        textures::copy(commandBuffer, fftData.signal_real[1], frequency.realBuffer, {1, N});
        textures::copy(commandBuffer, fftData.signal_imaginary[1], frequency.imaginaryBuffer, {1, N});
    });
}

void SignalProcessing::compute2DFFT(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = fftData.signalDescriptorSets[0];
    sets[1] = fftData.signalDescriptorSets[1];
    sets[2] = fftData.lookupDescriptorSet;

    compute2DFFT(commandBuffer, sets, fftData.signal_real, fftData.signal_imaginary);
}

void SignalProcessing::compute2DInverseFFT(VkCommandBuffer commandBuffer) {
    textures::copy(commandBuffer, fftData.signal_real[0], inverseFFTData.signal_real[0]);
    textures::copy(commandBuffer, fftData.signal_imaginary[0], inverseFFTData.signal_imaginary[0]);

    static std::array<VkDescriptorSet, 3> sets;
    sets[0] = inverseFFTData.signalDescriptorSets[0];
    sets[1] = inverseFFTData.signalDescriptorSets[1];
    sets[2] = inverseFFTData.lookupDescriptorSet;

    compute2DFFT(commandBuffer, sets, inverseFFTData.signal_real, inverseFFTData.signal_imaginary);
}

void SignalProcessing::applyMask(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = fftData.signalDescriptorSets[0];
    sets[1] = compute_mask.descriptorSet;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, apply_mask.layout.handle, 0, sets.size(), sets.data(), 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, apply_mask.pipeline.handle);
    vkCmdDispatch(commandBuffer, N, N, 1);
}

void SignalProcessing::compute2DFFT(VkCommandBuffer commandBuffer, std::array<VkDescriptorSet, 3> &sets, std::array<Texture, 2>& signal_real, std::array<Texture, 2>& signal_imaginary) {
    auto passes = static_cast<int>(std::log2(N));

    // fft along x-axis
    for(auto pass = 0; pass < passes; ++pass){

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.pipeline_horizontal.handle);
        vkCmdPushConstants(commandBuffer, compute_fft.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
        vkCmdDispatch(commandBuffer, N, N, 1);

        auto output = 1 - (pass % 2);
        addImageMemoryBarriers(commandBuffer, { signal_real[output].image, signal_imaginary[output].image});
        std::swap(sets[0], sets[1]);
    }

    // fft along y-axis
    for(auto pass = 0; pass < passes; ++pass){
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.layout.handle, 0, COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_fft.pipeline_vertical.handle);
        vkCmdPushConstants(commandBuffer, compute_fft.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int), &pass);
        vkCmdDispatch(commandBuffer, N, N, 1);
        auto output = (pass % 2);
        addImageMemoryBarriers(commandBuffer, { signal_real[output].image, signal_imaginary[output].image});
        if(pass < passes - 1){
            std::swap(sets[0], sets[1]);
        }
    }
}

void SignalProcessing::computeLuminance(VkCommandBuffer commandBuffer) {
    grayscaleTexture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE,
                                       VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_luminance.layout.handle, 0, 1, &compute_luminance.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_luminance.pipeline.handle);
    vkCmdDispatch(commandBuffer, N, N, 1);

    grayscaleTexture.image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                              DEFAULT_SUB_RANGE, VK_ACCESS_SHADER_WRITE_BIT,
                                              VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void SignalProcessing::prepFFTForRender(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = fftData.signalDescriptorSets[0];
    sets[1] = fftDisplayDescriptorSet;
    prepFFTForRender(commandBuffer, sets, fourierRenderImage.image);
}

void SignalProcessing::prepInverseFFTForRender(VkCommandBuffer commandBuffer) {
    static std::array<VkDescriptorSet, 2> sets;
    sets[0] = inverseFFTData.signalDescriptorSets[0];
    sets[1] = inverseDisplayDescriptorSet;
    prepFFTForRender(commandBuffer, sets, inverseFFTTexture.image, 1);
}

void SignalProcessing::prepFFTForRender(VkCommandBuffer commandBuffer, const std::array<VkDescriptorSet, 2> &sets,
                                        VulkanImage &image, int reverse) {


    auto exec = [&](int pass) {
        image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_GENERAL, DEFAULT_SUB_RANGE,
                                                  VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT,
                                                  VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        static std::array<int, 2> constants;
        constants[0] = pass;
        constants[1] = reverse;

        if(pass == 0){
            vkCmdFillBuffer(commandBuffer, fft_prep_render.maxMagnitudeBuffer, 0, sizeof(int), 0);
        }

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, fft_prep_render.layout.handle, 0,
                                COUNT(sets), sets.data(), 0, VK_NULL_HANDLE);
        vkCmdPushConstants(commandBuffer, fft_prep_render.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, BYTE_SIZE(constants), constants.data());
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, fft_prep_render.pipeline.handle);
        vkCmdDispatch(commandBuffer, N, N, 1);

        image.transitionLayout(commandBuffer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                  DEFAULT_SUB_RANGE, VK_ACCESS_SHADER_WRITE_BIT,
                                                  VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    };

    exec(0);
    addImageMemoryBarriers(commandBuffer, { image});
    exec(1);
}

void SignalProcessing::computeMask(VkCommandBuffer commandBuffer) {


    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_mask.layout.handle, 0, 1, &compute_mask.descriptorSet, 0, VK_NULL_HANDLE);
    vkCmdPushConstants(commandBuffer, compute_mask.layout.handle, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute_mask.constants), &compute_mask.constants);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute_mask.pipeline.handle);
    vkCmdDispatch(commandBuffer, N, N, 1);


}

void SignalProcessing:: loadImageSignal() {
    textures::fromFile(device, imageSignal, resource(images[selectedImage]));
    textures::create(device, grayscaleTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
//    imageSignalTexId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(grayscaleTexture.imageView);

    textures::create(device, fourierRenderImage, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    imageFourierXformSignalTexId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(fourierRenderImage.imageView);

    textures::create(device, inverseFFTTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    imageSignalTexId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(inverseFFTTexture.imageView);

    textures::create(device, maskTexture, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {N, N, 1});
    maskTextureId = plugin<ImGuiPlugin>(IM_GUI_PLUGIN).addTexture(maskTexture.imageView);

    textures::create(device, fftData.signal_real[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, signal.real, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, fftData.signal_imaginary[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, signal.imaginary, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, fftData.signal_real[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, fftData.signal_imaginary[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));

    textures::create(device, inverseFFTData.signal_real[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, signal.real, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, inverseFFTData.signal_imaginary[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, signal.imaginary, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, inverseFFTData.signal_real[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));
    textures::create(device, inverseFFTData.signal_imaginary[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32_SFLOAT, {N, N, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT, sizeof(float));

    fftData.signal_real[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    fftData.signal_imaginary[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    fftData.signal_real[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    fftData.signal_imaginary[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

    inverseFFTData.signal_real[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inverseFFTData.signal_imaginary[0].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inverseFFTData.signal_real[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    inverseFFTData.signal_imaginary[1].image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);

}


void SignalProcessing::createDescriptorPool() {
    constexpr uint32_t maxSets = 100;
    std::array<VkDescriptorPoolSize, 16> poolSizes{
            {
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets},
                    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets},
                    { VK_DESCRIPTOR_TYPE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT, 100 * maxSets },
                    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 * maxSets }
            }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);

}

void SignalProcessing::createDescriptorSetLayouts() {
    signalDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_signal")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    lookupDescriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_butterfly_lookup")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    compute_luminance.setLayout =
        device.descriptorSetLayoutBuilder()
            .name("compute_luminance")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(2)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    fft_prep_render.setLayout =
        device.descriptorSetLayoutBuilder()
            .name("fft_prep_render")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
            .binding(1)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    compute_mask.descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("compute_mask")
            .binding(0)
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_COMPUTE_BIT)
        .createLayout();

    auto sets = descriptorPool.allocate({ signalDescriptorSetLayout
                                                , signalDescriptorSetLayout
                                                , lookupDescriptorSetLayout
                                                , compute_luminance.setLayout
                                                , fft_prep_render.setLayout
                                                , compute_mask.descriptorSetLayout
                                                , signalDescriptorSetLayout
                                                , signalDescriptorSetLayout
                                                , lookupDescriptorSetLayout
                                                , fft_prep_render.setLayout});

    fftData.signalDescriptorSets[0] = sets[0];
    fftData.signalDescriptorSets[1] = sets[1];
    fftData.lookupDescriptorSet = sets[2];
    compute_luminance.descriptorSet = sets[3];
    fftDisplayDescriptorSet = sets[4];
    compute_mask.descriptorSet = sets[5];

    inverseFFTData.signalDescriptorSets[0] = sets[6];
    inverseFFTData.signalDescriptorSets[1] = sets[7];
    inverseFFTData.lookupDescriptorSet = sets[8];
    inverseDisplayDescriptorSet = sets[9];

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_signal_ping", fftData.signalDescriptorSets[0]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_signal_pong", fftData.signalDescriptorSets[1]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_lookup_table", fftData.lookupDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("fft_display", fftDisplayDescriptorSet);

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("gray_scale", compute_luminance.descriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("mask", compute_mask.descriptorSet);

    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_signal_ping", inverseFFTData.signalDescriptorSets[0]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_signal_pong", inverseFFTData.signalDescriptorSets[1]);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_lookup_table", inverseFFTData.lookupDescriptorSet);
    device.setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("inverse_fft_display", inverseDisplayDescriptorSet);

}

void SignalProcessing::createComputeFFTPipeline() {
    auto module = device.createShaderModule(resource("fft_butterfly_horizontal.comp.spv"));
    auto stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});

    compute_fft.layout = device.createPipelineLayout(
            { signalDescriptorSetLayout,
              signalDescriptorSetLayout,
              lookupDescriptorSetLayout
            }, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int)} });

    auto createInfo = initializers::computePipelineCreateInfo();
    createInfo.stage = stage;
    createInfo.layout = compute_fft.layout.handle;
    compute_fft.pipeline_horizontal = device.createComputePipeline(createInfo);

    module = device.createShaderModule(resource("fft_butterfly_vertical.comp.spv"));
    stage = initializers::shaderStage({module, VK_SHADER_STAGE_COMPUTE_BIT});
    createInfo.stage = stage;
    createInfo.layout = compute_fft.layout.handle;
    compute_fft.pipeline_vertical = device.createComputePipeline(createInfo);

    compute_luminance.layout = device.createPipelineLayout({ compute_luminance.setLayout });
    module = device.createShaderModule(resource("luminance.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    createInfo.stage = stage;
    createInfo.layout = compute_luminance.layout.handle;
    compute_luminance.pipeline = device.createComputePipeline(createInfo);

    fft_prep_render.layout = device.createPipelineLayout(
            { signalDescriptorSetLayout,
              fft_prep_render.setLayout
            }, { {VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(int) * 2} });
    module = device.createShaderModule(resource("fft_render.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    createInfo.stage = stage;
    createInfo.layout = fft_prep_render.layout.handle;
    fft_prep_render.pipeline = device.createComputePipeline(createInfo);

    compute_mask.layout = device.createPipelineLayout(
            { compute_mask.descriptorSetLayout},
            {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(compute_mask.constants)}} );

    module = device.createShaderModule(resource("mask.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    createInfo.stage = stage;
    createInfo.layout = compute_mask.layout.handle;
    compute_mask.pipeline = device.createComputePipeline(createInfo);

    apply_mask.layout = device.createPipelineLayout(
            {signalDescriptorSetLayout, compute_mask.descriptorSetLayout});

    module = device.createShaderModule(resource("apply_mask.comp.spv"));
    stage = initializers::shaderStage({ module, VK_SHADER_STAGE_COMPUTE_BIT});
    createInfo.stage = stage;
    createInfo.layout = apply_mask.layout.handle;
    apply_mask.pipeline = device.createComputePipeline(createInfo);

}

void SignalProcessing::updateDescriptorSets(){
    auto writes = initializers::writeDescriptorSets<20>();
    
    writes[0].dstSet = fftData.signalDescriptorSets[0];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    VkDescriptorImageInfo realImgInInfo{ VK_NULL_HANDLE, fftData.signal_real[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[0].pImageInfo = &realImgInInfo;

    writes[1].dstSet = fftData.signalDescriptorSets[0];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    VkDescriptorImageInfo imaginaryImgInInfo{ VK_NULL_HANDLE, fftData.signal_imaginary[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[1].pImageInfo = &imaginaryImgInInfo;

    writes[2].dstSet = fftData.signalDescriptorSets[1];
    writes[2].dstBinding = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].descriptorCount = 1;
    VkDescriptorImageInfo realImgOutInfo{ VK_NULL_HANDLE, fftData.signal_real[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[2].pImageInfo = &realImgOutInfo;

    writes[3].dstSet = fftData.signalDescriptorSets[1];
    writes[3].dstBinding = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 1;
    VkDescriptorImageInfo imaginaryImgOutInInfo{ VK_NULL_HANDLE, fftData.signal_imaginary[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[3].pImageInfo = &imaginaryImgOutInInfo;

    writes[4].dstSet = fftData.lookupDescriptorSet;
    writes[4].dstBinding = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    VkDescriptorImageInfo lookupIndexInfo{ fftData.butterfly.index.sampler.handle, fftData.butterfly.index.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[4].pImageInfo = &lookupIndexInfo;

    writes[5].dstSet = fftData.lookupDescriptorSet;
    writes[5].dstBinding = 1;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].descriptorCount = 1;
    VkDescriptorImageInfo butterflyLookupInfo{ fftData.butterfly.lut.sampler.handle, fftData.butterfly.lut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[5].pImageInfo = &butterflyLookupInfo;

    writes[6].dstSet = compute_luminance.descriptorSet;
    writes[6].dstBinding = 0;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[6].descriptorCount = 1;
    VkDescriptorImageInfo srcImageInfo{ imageSignal.sampler.handle, imageSignal.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[6].pImageInfo = &srcImageInfo;

    writes[7].dstSet = compute_luminance.descriptorSet;
    writes[7].dstBinding = 1;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[7].descriptorCount = 1;
    VkDescriptorImageInfo dstImageInfo{ VK_NULL_HANDLE, fftData.signal_real[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[7].pImageInfo = &dstImageInfo;

    writes[8].dstSet = compute_luminance.descriptorSet;
    writes[8].dstBinding = 2;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[8].descriptorCount = 1;
    VkDescriptorImageInfo grayScaleImageInfo{ VK_NULL_HANDLE, grayscaleTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[8].pImageInfo = &grayScaleImageInfo;

    writes[9].dstSet = fftDisplayDescriptorSet;
    writes[9].dstBinding = 0;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[9].descriptorCount = 1;
    VkDescriptorImageInfo fftPrepImageInfo{ VK_NULL_HANDLE, fourierRenderImage.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[9].pImageInfo = &fftPrepImageInfo;
    
    writes[10].dstSet = fftDisplayDescriptorSet;
    writes[10].dstBinding = 1;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    VkDescriptorBufferInfo magInfo{fft_prep_render.maxMagnitudeBuffer, 0, VK_WHOLE_SIZE};
    writes[10].pBufferInfo = &magInfo;

    writes[11].dstSet = compute_mask.descriptorSet;
    writes[11].dstBinding = 0;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[11].descriptorCount = 1;
    VkDescriptorImageInfo maskInfo{ VK_NULL_HANDLE, maskTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[11].pImageInfo = &maskInfo;

    writes[12].dstSet = inverseFFTData.signalDescriptorSets[0];
    writes[12].dstBinding = 0;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[12].descriptorCount = 1;
    VkDescriptorImageInfo inverseRealImgInInfo{ VK_NULL_HANDLE, inverseFFTData.signal_real[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[12].pImageInfo = &inverseRealImgInInfo;

    writes[13].dstSet = inverseFFTData.signalDescriptorSets[0];
    writes[13].dstBinding = 1;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[13].descriptorCount = 1;
    VkDescriptorImageInfo inverseImaginaryImgInInfo{ VK_NULL_HANDLE, inverseFFTData.signal_imaginary[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[13].pImageInfo = &inverseImaginaryImgInInfo;

    writes[14].dstSet = inverseFFTData.signalDescriptorSets[1];
    writes[14].dstBinding = 0;
    writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[14].descriptorCount = 1;
    VkDescriptorImageInfo inverseRealImgOutInfo{ VK_NULL_HANDLE, inverseFFTData.signal_real[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[14].pImageInfo = &inverseRealImgOutInfo;

    writes[15].dstSet = inverseFFTData.signalDescriptorSets[1];
    writes[15].dstBinding = 1;
    writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[15].descriptorCount = 1;
    VkDescriptorImageInfo inverseImaginaryImgOutInInfo{ VK_NULL_HANDLE, inverseFFTData.signal_imaginary[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[15].pImageInfo = &inverseImaginaryImgOutInInfo;

    writes[16].dstSet = inverseFFTData.lookupDescriptorSet;
    writes[16].dstBinding = 0;
    writes[16].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[16].descriptorCount = 1;
    VkDescriptorImageInfo inverseLookupIndexInfo{ inverseFFTData.butterfly.index.sampler.handle, inverseFFTData.butterfly.index.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[16].pImageInfo = &inverseLookupIndexInfo;

    writes[17].dstSet = inverseFFTData.lookupDescriptorSet;
    writes[17].dstBinding = 1;
    writes[17].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[17].descriptorCount = 1;
    VkDescriptorImageInfo inverseButterflyLookupInfo{ inverseFFTData.butterfly.lut.sampler.handle, inverseFFTData.butterfly.lut.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    writes[17].pImageInfo = &inverseButterflyLookupInfo;

    writes[18].dstSet = inverseDisplayDescriptorSet;
    writes[18].dstBinding = 0;
    writes[18].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[18].descriptorCount = 1;
    VkDescriptorImageInfo invFFTPrepImageInfo{ VK_NULL_HANDLE, inverseFFTTexture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL};
    writes[18].pImageInfo = &invFFTPrepImageInfo;

    writes[19].dstSet = inverseDisplayDescriptorSet;
    writes[19].dstBinding = 1;
    writes[19].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[19].descriptorCount = 1;
    writes[19].pBufferInfo = &magInfo;

    device.updateDescriptorSets(writes);
}

void SignalProcessing::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    commandBuffers = commandPool.allocateCommandBuffers(swapChainImageCount);
}

void SignalProcessing::createPipelineCache() {
    pipelineCache = device.createPipelineCache();
}


void SignalProcessing::onSwapChainDispose() {
}

void SignalProcessing::onSwapChainRecreation() {
    updateDescriptorSets();
}

VkCommandBuffer *SignalProcessing::buildCommandBuffers(uint32_t imageIndex, uint32_t &numCommandBuffers) {
    numCommandBuffers = 1;
    auto& commandBuffer = commandBuffers[imageIndex];

    VkCommandBufferBeginInfo beginInfo = initializers::commandBufferBeginInfo();
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    static std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = {0, 0, 0, 0};
    clearValues[1].depthStencil = {1.0, 0u};

    VkRenderPassBeginInfo rPassInfo = initializers::renderPassBeginInfo();
    rPassInfo.clearValueCount = COUNT(clearValues);
    rPassInfo.pClearValues = clearValues.data();
    rPassInfo.framebuffer = framebuffers[imageIndex];
    rPassInfo.renderArea.offset = {0u, 0u};
    rPassInfo.renderArea.extent = swapChain.extent;
    rPassInfo.renderPass = renderPass;

    vkCmdBeginRenderPass(commandBuffer, &rPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    plotGraph(commandBuffer);

    vkCmdEndRenderPass(commandBuffer);

    vkEndCommandBuffer(commandBuffer);

    return &commandBuffer;
}

void SignalProcessing::renderImage() {
    float w = 512;
    float h = 512;
    ImGui::Begin("2D Fourier Transform");
    ImGui::SetWindowSize({0, 0});
    ImGui::Image(imageSignalTexId, {w, h}); ImGui::SameLine();
    ImGui::Image(imageFourierXformSignalTexId, {w, h});
    recomputeDFT |= ImGui::Combo("image", &selectedImage, images.data(), images.size());
    ImGui::Image(maskTextureId, {w, h});
    std::array<const char*, 5> filters{"None", "Ideal", "Gaussian", "Butterworth", "box"};
    recomputeDFT |= ImGui::Combo("filters", &compute_mask.constants.maskId, filters.data(), filters.size());
    recomputeDFT |= ImGui::RadioButton("low pass", &compute_mask.constants.inverse, 0); ImGui::SameLine();
    recomputeDFT |= ImGui::RadioButton("high pass", &compute_mask.constants.inverse, 1);
    if(compute_mask.constants.maskId != 0){
        recomputeDFT |= ImGui::SliderFloat("radius", &compute_mask.constants.d0, 0.01, 0.49);
    }
    if(compute_mask.constants.maskId == 3){
        recomputeDFT |= ImGui::SliderInt("order", &compute_mask.constants.n, 1, 5);
    }
    ImGui::End();
}

void SignalProcessing::plotGraph(VkCommandBuffer commandBuffer) {
    ImGui::Begin("Time domain");
    ImGui::SetWindowSize({1000, 350});

    if(ImPlot::BeginPlot("Signal")){
        ImPlot::PlotLine("cosine", signal.xData.data(), signal.real, N);
        ImPlot::EndPlot();
    }
    ImGui::End();

    ImGui::Begin("Frequency domain");
    ImGui::SetWindowSize({1000, 350});

    if(ImPlot::BeginPlot("Frequency")){
        ImPlot::PlotLine("real", frequency.xData.data(), frequency.real, N/2);
        ImPlot::PlotLine("imaginary", frequency.xData.data(), frequency.imaginary, N/2);
        ImPlot::EndPlot();
    }
    ImGui::End();

    ImGui::Begin("Filters");
    ImGui::SetWindowSize({1000, 350});

    if(ImPlot::BeginPlot("Filters")){
        ImPlot::PlotLine("box filter", boxFilter.xData.data(), boxFilter.real, N);
        ImPlot::PlotLine("sinc filter", SincFilter.xData.data(), SincFilter.real, N);
        ImPlot::EndPlot();
    }
    ImGui::End();

    renderImage();
    plugin(IM_GUI_PLUGIN).draw(commandBuffer);
}

void SignalProcessing::update(float time) {
    camera->update(time);
    auto cam = camera->cam();
}

void SignalProcessing::checkAppInputs() {
    camera->processInput();
}

void SignalProcessing::cleanup() {
    VulkanBaseApp::cleanup();
}

void SignalProcessing::onPause() {
    VulkanBaseApp::onPause();
}

void SignalProcessing::newFrame() {
    if(!ImGui::IsAnyItemActive() && recomputeDFT){
        device.wait();
        if(previousSelectedImage != selectedImage){
            previousSelectedImage = selectedImage;
            loadImageSignal();
            updateDescriptorSets();
        }
        recomputeDFT = false;
        run2DFFT();
    }
}


int main(){
    try{

        Settings settings;
        settings.width = 2500;
        settings.height = 1200;
        settings.depthTest = true;

        auto app = SignalProcessing{ settings };
        std::unique_ptr<Plugin> imGui = std::make_unique<ImGuiPlugin>();
        app.addPlugin(imGui);
        app.run();
    }catch(std::runtime_error& err){
        spdlog::error(err.what());
    }
}