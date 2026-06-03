#include <WaterSimulation.hpp>

#include "AppContext.hpp"
#include "Barrier.hpp"
#include "ImGuiPlugin.hpp"
#include "constants.hpp"
#include "descriptor_utils.hpp"
#include "filemanager.hpp"

#include <imgui.h>

namespace {
    constexpr auto InitializeSpectrum = "InitializeSpectrum";
    constexpr auto Dispersion = "Dispersion";
    constexpr auto InverseFFTRow = "InverseFFTRow";
    constexpr auto InverseFFTColumn = "InverseFFTColumn";
    constexpr auto EvaluateSurfaceGradients = "EvaluateSurfaceGradients";
    constexpr auto Visualizer = "Visualizer";
    constexpr uint32_t WorkgroupResolution = 8;

    void transition_to_general(const VulkanDevice& device, Texture& texture) {
        texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL);
    }

}

WaterSimulation::WaterSimulation(VulkanDevice &device) : m_device(&device) {}

void WaterSimulation::initialize() {
    const auto w = g_WaterSimResolution;
    for (auto i = 0; i < g_WaterSimBandCount; ++i) {
        textures::create(*m_device, m_HImaginaryTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_HImaginaryTexture[i]);
        textures::create(*m_device, m_FFTRowPassRealTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_FFTRowPassRealTexture[i]);
        textures::create(*m_device, m_FFTRowPassImaginaryTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_FFTRowPassImaginaryTexture[i]);
    }
    textures::create(*m_device, m_visualizer.texture, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
    transition_to_general(*m_device, m_visualizer.texture);

    createDescriptorSetLayout();
    updateDescriptorSet();
    createPipelines();

}

void WaterSimulation::evaluate(VkCommandBuffer commandBuffer, WaterData &waterData) {
    m_device->section([&] {
        const auto tileCount = g_WaterSimResolution / WorkgroupResolution;
        if (!waterData.valid_spectrum()) {
            dispatch(commandBuffer, waterData, InitializeSpectrum, tileCount, tileCount, g_WaterSimBandCount);
        }
        waterData.validate_spectrum();

        dispatch(commandBuffer, waterData, Dispersion, tileCount, tileCount, g_WaterSimBandCount);
        dispatch(commandBuffer, waterData, InverseFFTRow, 1, g_WaterSimResolution, g_WaterSimBandCount);
        dispatch(commandBuffer, waterData, InverseFFTColumn, 1, g_WaterSimResolution, g_WaterSimBandCount);
        dispatch(commandBuffer, waterData, EvaluateSurfaceGradients, tileCount, tileCount, g_WaterSimBandCount);
        generateMipMaps(commandBuffer, waterData);
    }, commandBuffer, "water_simulation");
}

void WaterSimulation::visualizer(ImGuiPlugin& plugin) {
    static ImTextureID textureId = plugin.addTexture(m_visualizer.texture, VK_IMAGE_LAYOUT_GENERAL);
    static std::array<const char*, 6> viewLabels{
        "Spectrum",
        "H imaginary",
        "Displacement",
        "FFT row real",
        "FFT row imaginary",
        "Surface gradient",
    };
    static std::array<const char*, g_WaterSimBandCount> bandLabels{ "0", "1", "2", "3" };

    ImGui::Begin("water simulation visualizer");
    ImGui::SetWindowSize({ 0, 0 });
    ImGui::Image(textureId, { 300, 300 });
    ImGui::Combo("View", &m_visualizer.constants.view, viewLabels.data(), viewLabels.size());
    ImGui::Combo("Band", &m_visualizer.constants.band, bandLabels.data(), bandLabels.size());
    ImGui::SliderFloat("Scale", &m_visualizer.constants.scale, 0.01f, 2.0f);
    ImGui::End();
}

void WaterSimulation::dispatch(VkCommandBuffer commandBuffer, const WaterData& waterData, const char* pipeline, uint32_t x, uint32_t y, uint32_t z) {
    const std::array descriptorSets{ waterData.descriptor_set(), m_descriptorSet };
    const auto layout = m_compute.layout(pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(pipeline));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    vkCmdDispatch(commandBuffer, x, y, z);
    Barrier::computeWriteToRead(commandBuffer);
}

void WaterSimulation::generateMipMaps(VkCommandBuffer commandBuffer, WaterData& waterData) {
    for (auto& texture : waterData.get_sg_texture()) {
        textures::generateLOD(commandBuffer, texture.image, texture.width, texture.height, texture.levels);
    }
}

void WaterSimulation::visualize(VkCommandBuffer commandBuffer, const WaterData& waterData) {
    const std::array descriptorSets{ waterData.descriptor_set(), m_descriptorSet };
    const auto layout = m_compute.layout(Visualizer);
    const auto tileCount = g_WaterSimResolution / WorkgroupResolution;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_compute.pipeline(Visualizer));
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, descriptorSets.size(), descriptorSets.data(), 0, nullptr);
    vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_visualizer.constants), &m_visualizer.constants);
    vkCmdDispatch(commandBuffer, tileCount, tileCount, 1);
    Barrier::computeWriteToFragmentRead(commandBuffer);
}

void WaterSimulation::createDescriptorSetLayout() {
    m_descriptorSetLayout =
        m_device->descriptorSetLayoutBuilder()
            .name("water_sim_descriptor_set_layout")
            .binding(0) // m_HImaginaryTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1) // m_FFTRowPassRealTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2) // m_FFTRowPassImaginaryTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3) // m_visualizer.texture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();
}

void WaterSimulation::updateDescriptorSet() {
    m_descriptorSet = AppContext::descriptorPool().allocate({ m_descriptorSetLayout }).front();
    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("water_sim_descriptor_set", m_descriptorSet);
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> HImaginaryTextureInfos{};
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> FFTRowPassRealTextureInfos{};
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> FFTRowPassImaginaryTextureInfos{};

    for (uint32_t idx = 0; idx < g_WaterSimBandCount; ++idx) {
        HImaginaryTextureInfos[idx] = { VK_NULL_HANDLE, m_HImaginaryTexture[idx].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
        FFTRowPassRealTextureInfos[idx] = { VK_NULL_HANDLE, m_FFTRowPassRealTexture[idx].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
        FFTRowPassImaginaryTextureInfos[idx] = { VK_NULL_HANDLE, m_FFTRowPassImaginaryTexture[idx].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    }

    const VkDescriptorImageInfo visualizerInfo{ VK_NULL_HANDLE, m_visualizer.texture.imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
    auto writes = initializers::writeDescriptorSets<4>(m_descriptorSet);

    set_image_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, HImaginaryTextureInfos.data(), g_WaterSimBandCount);
    set_image_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FFTRowPassRealTextureInfos.data(), g_WaterSimBandCount);
    set_image_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, FFTRowPassImaginaryTextureInfos.data(), g_WaterSimBandCount);
    set_image_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &visualizerInfo);

    m_device->updateDescriptorSets(writes);
}

void WaterSimulation::createPipelines() {
    m_compute = ComputePipelines{ m_device, metadata() };
    m_compute.createPipelines();
}


std::vector<PipelineMetaData> WaterSimulation::metadata() {
    return {
        {
            .name = InitializeSpectrum,
            .shadePath = FileManager::resource("water_initialize_spectrum.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
        },
        {
            .name = Dispersion,
            .shadePath = FileManager::resource("water_dispersion.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
        },
        {
            .name = InverseFFTRow,
            .shadePath = FileManager::resource("water_inverse_fft_row.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
        },
        {
            .name = InverseFFTColumn,
            .shadePath = FileManager::resource("water_inverse_fft_column.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
        },
        {
            .name = EvaluateSurfaceGradients,
            .shadePath = FileManager::resource("water_evaluate_surface_gradients.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
        },
        {
            .name = Visualizer,
            .shadePath = FileManager::resource("water_visualizer.comp.spv"),
            .layouts = { &WaterData::descriptorSetLayout, &m_descriptorSetLayout },
            .ranges = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(m_visualizer.constants) } },
        },
    };
}
