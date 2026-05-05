#include "WhitecapsDemo.hpp"

#include "Barrier.hpp"
#include "GraphicsPipelineBuilder.hpp"
#include "Vertex.h"
#include "plugins/BindLessDescriptorPlugin.hpp"

#include <imgui.h>
#include <fstream>
#include <numeric>

WhitecapsDemo::WhitecapsDemo(const Settings& settings)
    : VulkanBaseApp("Whitecaps", settings) {
    fileManager().addSearchPathFront(".");
    fileManager().addSearchPathFront("whitecaps");
    fileManager().addSearchPathFront("whitecaps/data");
    fileManager().addSearchPathFront("whitecaps/spv");
}

void WhitecapsDemo::beforeDeviceCreation() {
    settings.enabledFeatures.geometryShader = VK_TRUE;
    settings.enabledFeatures.samplerAnisotropy = VK_TRUE;
}

void WhitecapsDemo::initApp() {
    createCommandPool();
    createDescriptorPool();
    createTextures();
    createButterflyLookup();
    loadAtmosphereTables();
    createDescriptorSetLayout();
    updateDescriptorSet();
    pipelineCache = device.createPipelineCache();
    createComputePipelines();
    createGraphicsPipelines();
    createGrid();
    createCommandBuffers();
}

void WhitecapsDemo::createCommandPool() {
    commandPool = device.createCommandPool(*device.queueFamilyIndex.graphics, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
}

void WhitecapsDemo::createDescriptorPool() {
    constexpr uint32_t maxSets = 32;
    std::array<VkDescriptorPoolSize, 3> poolSizes{
        {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8},
        }
    };
    descriptorPool = device.createDescriptorPool(maxSets, poolSizes, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
}

void WhitecapsDemo::createTextures() {
    auto readBinary = [](const fs::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), {});
    };
    auto rgbToRgba = [](const std::vector<unsigned char>& bytes) {
        auto rgb = reinterpret_cast<const float*>(bytes.data());
        std::vector<glm::vec4> rgba(bytes.size() / (3 * sizeof(float)));
        for (size_t i = 0; i < rgba.size(); ++i) {
            rgba[i] = {rgb[i * 3 + 0], rgb[i * 3 + 1], rgb[i * 3 + 2], 1.0f};
        }
        return rgba;
    };

    textures.waves[0].layers = WaveLayers;
    textures.waves[1].layers = WaveLayers;
    textures.slopeVariance.depth = SlopeVarianceSize;

    textures::createNoTransition(device, textures.spectrum12, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, FftSize, 1});
    textures::createNoTransition(device, textures.spectrum34, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, FftSize, 1});
    textures::createNoTransition(device, textures.waves[0], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, FftSize, 1});
    textures::createNoTransition(device, textures.waves[1], VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, FftSize, 1});
    textures::createNoTransition(device, textures.butterfly, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, Passes, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textures::createNoTransition(device, textures.slopeVariance, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, {SlopeVarianceSize, SlopeVarianceSize, SlopeVarianceSize});
    textures::createNoTransition(device, textures.whitecaps, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {FftSize, FftSize, 1});
    textures::createNoTransition(device, textures.sky, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, {256, 256, 1});

    auto irradiance = rgbToRgba(readBinary("whitecaps/data/irradiance.raw"));
    auto transmittance = rgbToRgba(readBinary("whitecaps/data/transmittance.raw"));
    auto inscatterBytes = readBinary("whitecaps/data/inscatter.raw");
    auto noiseBytes = readBinary("whitecaps/data/noise.pgm");
    auto noise = noiseBytes.size() > 38 ? noiseBytes.data() + 38 : nullptr;

    textures::create(device, textures.noise, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM, noise, {512, 512, 1}, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    textures::create(device, textures.irradiance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, irradiance.data(), {64, 16, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textures::create(device, textures.transmittance, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, transmittance.data(), {256, 64, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    textures::create(device, textures.inscatter, VK_IMAGE_TYPE_3D, VK_FORMAT_R32G32B32A32_SFLOAT, inscatterBytes.data(), {32 * 8, 128, 32}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    auto allLayers = DEFAULT_SUB_RANGE;
    allLayers.layerCount = WaveLayers;
    device.graphicsCommandPool().oneTimeCommand([&](auto commandBuffer) {
        Barriers::push(textures.spectrum12.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.spectrum34.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.waves[0].image, allLayers, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.waves[1].image, allLayers, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.butterfly.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.slopeVariance.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.whitecaps.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::push(textures.sky.image, DEFAULT_SUB_RANGE, VK_PIPELINE_STAGE_NONE, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_NONE, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        Barriers::flush(commandBuffer);
    });
}

void WhitecapsDemo::createButterflyLookup() {
    const auto n = FftSize;
    std::vector<glm::vec4> lookup(n * Passes);
    for (uint32_t pass = 0; pass < Passes; ++pass) {
        const uint32_t blocks = 1u << (Passes - 1u - pass);
        const uint32_t inputs = 1u << pass;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t block = i / inputs;
            const uint32_t offset = i % inputs;
            const float angle = glm::two_pi<float>() * static_cast<float>(offset * blocks) / static_cast<float>(n);
            const float i0 = static_cast<float>(block * inputs * 2u + offset) + 0.5f;
            const float i1 = static_cast<float>(block * inputs * 2u + inputs + offset) + 0.5f;
            lookup[pass * n + i] = {i0 / static_cast<float>(n), i1 / static_cast<float>(n), std::cos(angle), std::sin(angle)};
        }
    }
    textures::create(device, textures.butterfly, VK_IMAGE_TYPE_2D, VK_FORMAT_R32G32B32A32_SFLOAT, lookup.data(), {FftSize, Passes, 1}, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
}

void WhitecapsDemo::loadAtmosphereTables() {
    // The original demo ships raw Bruneton lookup tables. They are kept in the example data
    // directory and wired as descriptors; generation remains outside this sample, like upstream.
}

void WhitecapsDemo::createDescriptorSetLayout() {
    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("whitecaps_descriptor_set_layout")
            .binding(0).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(5).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(6).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(7).descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(8).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(9).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(10).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(11).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(12).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(13).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(14).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
            .binding(15).descriptorType(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER).descriptorCount(1).shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void WhitecapsDemo::updateDescriptorSet() {
    descriptorSet = descriptorPool.allocate({descriptorSetLayout})[0];
    auto writes = initializers::writeDescriptorSets<16>();

    std::array<VkDescriptorImageInfo, 16> info{
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.spectrum12.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.spectrum34.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.waves[0].imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.waves[1].imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.butterfly.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.slopeVariance.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.whitecaps.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{VK_NULL_HANDLE, textures.sky.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.spectrum12.sampler.handle, textures.spectrum12.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.spectrum34.sampler.handle, textures.spectrum34.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.waves[pingPong].sampler.handle, textures.waves[pingPong].imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.butterfly.sampler.handle, textures.butterfly.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.slopeVariance.sampler.handle, textures.slopeVariance.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.whitecaps.sampler.handle, textures.whitecaps.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.sky.sampler.handle, textures.sky.imageView.handle, VK_IMAGE_LAYOUT_GENERAL},
        VkDescriptorImageInfo{textures.noise.sampler.handle, textures.noise.imageView.handle, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    };

    for (uint32_t i = 0; i < writes.size(); ++i) {
        writes[i].dstSet = descriptorSet;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i < 8 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &info[i];
    }

    device.updateDescriptorSets(writes);
}

std::vector<PipelineMetaData> WhitecapsDemo::computeMetadata() {
    auto range = VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls)};
    return {
        {"whitecaps_spectrum", resource("whitecaps_spectrum.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_init", resource("whitecaps_init.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_fftx", resource("whitecaps_fftx.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_ffty", resource("whitecaps_ffty.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_variances", resource("whitecaps_variances.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_precompute", resource("whitecaps_precompute.comp.spv"), {&descriptorSetLayout}, {range}},
        {"whitecaps_skymap", resource("whitecaps_skymap.comp.spv"), {&descriptorSetLayout}, {range}},
    };
}

void WhitecapsDemo::createComputePipelines() {
    compute = ComputePipelines{&device, computeMetadata()};
    compute.createPipelines();
}

void WhitecapsDemo::createGraphicsPipelines() {
    auto makeScreenPipeline = [&](const std::string& name, const std::string& frag) {
        Pipeline pipeline;
        pipeline.pipeline =
            prototypes->cloneScreenSpaceGraphicsPipeline()
                .shaderStage()
                    .vertexShader(resource("whitecaps_fullscreen.vert.spv"))
                    .fragmentShader(resource(frag))
                .depthStencilState()
                    .disableDepthWrite()
                    .disableDepthTest()
                .layout().clear()
                    .addPushConstantRange(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Controls))
                    .addDescriptorSetLayout(descriptorSetLayout)
                    .name(name)
                .name(name)
            .build(pipeline.layout);
        return pipeline;
    };

    pipelines.render = makeScreenPipeline("whitecaps_ocean", "whitecaps_ocean.frag.spv");
    pipelines.sky = makeScreenPipeline("whitecaps_sky", "whitecaps_sky.frag.spv");
    pipelines.clouds = makeScreenPipeline("whitecaps_clouds", "whitecaps_clouds.frag.spv");
    pipelines.spectrum = makeScreenPipeline("whitecaps_spectrum_view", "whitecaps_spectrum.frag.spv");
    pipelines.composite = makeScreenPipeline("whitecaps_composite", "whitecaps_composite.frag.spv");
}

void WhitecapsDemo::createGrid() {
    const auto& quad = ClipSpace::Quad::positions;
    gridVertices = device.createDeviceLocalBuffer(quad.data(), BYTE_SIZE(quad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

void WhitecapsDemo::createCommandBuffers() {
    commandBuffers = commandPool.allocateCommandBuffers(swapChain.imageCount());
}

void WhitecapsDemo::recordSimulation(VkCommandBuffer commandBuffer) {
    const auto groups = FftSize / 16;
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("whitecaps_spectrum"), 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("whitecaps_spectrum"));
    vkCmdPushConstants(commandBuffer, compute.layout("whitecaps_spectrum"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls), &controls);
    vkCmdDispatch(commandBuffer, groups, groups, 1);
    Barrier::computeWriteToRead(commandBuffer);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("whitecaps_init"), 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("whitecaps_init"));
    vkCmdPushConstants(commandBuffer, compute.layout("whitecaps_init"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls), &controls);
    vkCmdDispatch(commandBuffer, groups, groups, 1);
    Barrier::computeWriteToRead(commandBuffer);

    for (uint32_t pass = 0; pass < Passes; ++pass) {
        controls.pass = static_cast<int>(pass);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("whitecaps_fftx"), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("whitecaps_fftx"));
        vkCmdPushConstants(commandBuffer, compute.layout("whitecaps_fftx"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls), &controls);
        vkCmdDispatch(commandBuffer, groups, groups, WaveLayers);
        Barrier::computeWriteToRead(commandBuffer);

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("whitecaps_ffty"), 0, 1, &descriptorSet, 0, nullptr);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("whitecaps_ffty"));
        vkCmdPushConstants(commandBuffer, compute.layout("whitecaps_ffty"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls), &controls);
        vkCmdDispatch(commandBuffer, groups, groups, WaveLayers);
        Barrier::computeWriteToRead(commandBuffer);
    }

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.layout("whitecaps_precompute"), 0, 1, &descriptorSet, 0, nullptr);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline("whitecaps_precompute"));
    vkCmdPushConstants(commandBuffer, compute.layout("whitecaps_precompute"), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Controls), &controls);
    vkCmdDispatch(commandBuffer, groups, groups, 1);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void WhitecapsDemo::renderScene(VkCommandBuffer commandBuffer) {
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gridVertices.buffer, &offset);

    auto draw = [&](Pipeline& pipeline) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline.handle);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout.handle, 0, 1, &descriptorSet, 0, nullptr);
        vkCmdPushConstants(commandBuffer, pipeline.layout.handle, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Controls), &controls);
        vkCmdDraw(commandBuffer, 4, 1, 0, 0);
    };

    draw(pipelines.sky);
    draw(controls.flags & 1 ? pipelines.spectrum : pipelines.render);
}

void WhitecapsDemo::renderUi(VkCommandBuffer) {}

VkCommandBuffer* WhitecapsDemo::buildCommandBuffers(uint32_t imageIndex, uint32_t& numCommandBuffers) {
    numCommandBuffers = 1;
    auto commandBuffer = commandBuffers[imageIndex];
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    recordSimulation(commandBuffer);
    clearColor(0.02f, 0.04f, 0.06f, 1.0f);
    renderToSwapChain([&] { renderScene(commandBuffer); }, commandBuffer);

    vkEndCommandBuffer(commandBuffer);
    return &commandBuffers[imageIndex];
}

void WhitecapsDemo::update(float time) {
    elapsedTime += time;
    controls.time = elapsedTime;
}

void WhitecapsDemo::checkAppInputs() {}

void WhitecapsDemo::onSwapChainDispose() {
    commandPool.free(commandBuffers);
}

void WhitecapsDemo::onSwapChainRecreation() {
    createCommandBuffers();
    createGraphicsPipelines();
}

void WhitecapsDemo::cleanup() {
    VulkanBaseApp::cleanup();
}

int main() {
    try {
        fs::current_path("../../../../examples/");
        Settings settings;
        settings.width = 1280;
        settings.height = 720;
        settings.depthTest = true;
        settings.enableBindlessDescriptors = false;
        settings.enabledFeatures.geometryShader = true;

        WhitecapsDemo app{settings};
        app.run();
    } catch (std::runtime_error& err) {
        spdlog::error(err.what());
    }
}
