#include "WaterData.hpp"
#include "AppContext.hpp"
#include "descriptor_utils.hpp"
#include "constants.hpp"

#include <imgui.h>

namespace {
    bool descriptorSetLayoutCreated = false;

    void transition_to_general(const VulkanDevice& device, Texture& texture) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, texture.levels, 0, texture.layers };
        texture.image.transitionLayout(device.graphicsCommandPool(), VK_IMAGE_LAYOUT_GENERAL, range);
    }
}

VulkanDescriptorSetLayout WaterData::descriptorSetLayout;

WaterData::WaterData(VulkanDevice &device) : m_device(&device) {}

void WaterData::initialize() {
    const auto w = g_WaterSimResolution;
    for (auto i = 0; i < g_WaterSimBandCount; ++i) {
        textures::create(*m_device, m_SpectrumTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_SpectrumTexture[i]);

        textures::create(*m_device, m_DisplacementTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_DisplacementTexture[i]);

        m_SurfaceGradientTexture[i].levels = g_WaterSimSurfaceGradientMipCount;
        textures::create(*m_device, m_SurfaceGradientTexture[i], VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT, {w, w, 1});
        transition_to_general(*m_device, m_SurfaceGradientTexture[i]);

        const VkImageSubresourceRange mip0Range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        m_SurfaceGradientMip0Views[i] =
            m_SurfaceGradientTexture[i].image.createView(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_VIEW_TYPE_2D, mip0Range);
    }

    m_DeformationCB.gpu = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(DeformationData), "water_deformation_data");
    m_DeformationCB.cpu = static_cast<DeformationData*>(m_DeformationCB.gpu.map());

    m_SimulationCB.gpu = m_device->createBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, sizeof(WaterSimulationData), "water_simulation_data");
    m_SimulationCB.cpu = static_cast<WaterSimulationData*>(m_SimulationCB.gpu.map());

    createDescriptorSetLayout();
    updateDescriptorSets();
}

void WaterData::reset_time() {
    m_AccumulatedTime = 0.0f;
}

void WaterData::wind_speed(const glm::vec4 &value) {
    m_WindSpeed = value;
}

void WaterData::dir_dampener(const glm::vec4 &value) {
    m_DirDampener = value;
}

void WaterData::choppiness(float value) {
    m_Choppiness = value;
}

void WaterData::amplification(float value) {
    m_Amplification = value;
}

const std::array<Texture, 4> & WaterData::get_spectrum_texture() const {
    return m_SpectrumTexture;
}

const std::array<Texture, 4> & WaterData::get_displacement_texture() const {
    return m_DisplacementTexture;
}

const std::array<Texture, 4> & WaterData::get_sg_texture() const {
    return m_SurfaceGradientTexture;
}

std::array<Texture, 4> & WaterData::get_sg_texture() {
    return m_SurfaceGradientTexture;
}

WaterSimulationCB WaterData::get_simulation_cb() const {
    return m_SimulationCB;
}

DeformationCB WaterData::get_deformation_cb() const {
    return m_DeformationCB;
}

void WaterData::update_simulation(const float deltaTime) {

    if (m_ActiveSimulation)
        m_AccumulatedTime += deltaTime * m_TimeMultiplier;
}

void WaterData::upload_constant_buffers() {
    WaterSimulationData& simCB = *m_SimulationCB.cpu;
    simCB.SimulationRes = g_WaterSimResolution;
    simCB.SimulationTime = m_AccumulatedTime;
    simCB.Choppiness = m_Choppiness;
    simCB.Amplification = m_Amplification;
    simCB.PatchSize = g_WaterSimPatchSize;
    simCB.PatchWindOrientation = { -glm::pi<float>() / 4.0f, glm::pi<float>() / 2.0f, glm::pi<float>() / 4.0f, 0 };
    simCB.PatchDirectionDampener = m_DirDampener;
    simCB.PatchWindSpeed = m_WindSpeed * g_KMPerHourToMPerSec;

    // Update deformation CB
    DeformationData& deformationCB = *m_DeformationCB.cpu;
    deformationCB.PatchSize = g_WaterSimPatchSize;
    deformationCB.Choppiness = m_Choppiness;
    deformationCB.Amplification = m_Amplification;
    deformationCB.PatchRoughness = g_WaterSimPatchRoughness;
    deformationCB.Attenuation = m_Attenuation ? 1 : 0;
    deformationCB.PatchFlags = (m_PatchFlag[0] ? 0x1 : 0x0) | (m_PatchFlag[1] ? 0x2 : 0x0) | (m_PatchFlag[2] ? 0x4 : 0x0) | (m_PatchFlag[3] ? 0x8 : 0x0);
}

void WaterData::render_ui_global() {
    ImGui::Checkbox("Water Simulation", &m_ActiveSimulation);
    ImGui::SliderFloat("Wind Speed0", &m_WindSpeed.x, 0.0f, 500.0f);
    ImGui::SliderFloat("Wind Speed1", &m_WindSpeed.y, 0.0f, 250.0f);
    ImGui::SliderFloat("Wind Speed2", &m_WindSpeed.z, 0.0f, 100.0f);
    ImGui::SliderFloat("Wind Speed3", &m_WindSpeed.w, 0.0f, 30.0f);
    ImGui::SliderFloat("Choppiness", &m_Choppiness, 0.0f, 5.0f);
    ImGui::SliderFloat("Time Multiplier", &m_TimeMultiplier, 0.0f, 5.0f);
}

void WaterData::render_ui_patch() {
    ImGui::Checkbox("Distance Attenuation", &m_Attenuation);
    ImGui::Checkbox("Band 0", &m_PatchFlag[0]);
    ImGui::SameLine();
    ImGui::Checkbox("Band 1", &m_PatchFlag[1]);
    ImGui::SameLine();
    ImGui::Checkbox("Band 2", &m_PatchFlag[2]);
    ImGui::SameLine();
    ImGui::Checkbox("Band 3", &m_PatchFlag[3]);
}

bool WaterData::valid_spectrum() const {
    using namespace glm;
    return m_Initialized
        && all(equal(m_InternalWindSpeed, m_WindSpeed))
        && all(equal(m_InternalDirDampener, m_DirDampener));
}

void WaterData::validate_spectrum() {
    m_Initialized = true;
    m_InternalWindSpeed = m_WindSpeed;
    m_InternalDirDampener = m_DirDampener;
}

void WaterData::createDescriptorSetLayout() {
    createDescriptorSetLayout(*m_device);
}

void WaterData::createDescriptorSetLayout(const VulkanDevice& device) {
    if (descriptorSetLayoutCreated) return;

    descriptorSetLayout =
        device.descriptorSetLayoutBuilder()
            .name("water_data")
            .binding(0) // DeformationCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(1) // WaterSimulationCB
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
                .descriptorCount(1)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(2) // m_SpectrumTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(3) // m_DisplacementTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
            .binding(4) // m_SurfaceGradientTexture
                .descriptorType(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
                .descriptorCount(g_WaterSimBandCount)
                .shaderStages(VK_SHADER_STAGE_ALL)
        .createLayout();

    descriptorSetLayoutCreated = true;
}

void WaterData::updateDescriptorSets() {
    m_descriptorSet = AppContext::descriptorPool().allocate({ descriptorSetLayout }).front();
    m_device->setName<VK_OBJECT_TYPE_DESCRIPTOR_SET>("water_data_descriptor_set", m_descriptorSet);

    const VkDescriptorBufferInfo deformationInfo = descriptor_buffer_info(m_DeformationCB.gpu);
    const VkDescriptorBufferInfo simulationInfo = descriptor_buffer_info(m_SimulationCB.gpu);
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> spectrumInfos{};
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> displacementInfos{};
    std::array<VkDescriptorImageInfo, g_WaterSimBandCount> surfaceGradientInfos{};

    for (uint32_t idx = 0; idx < g_WaterSimBandCount; ++idx) {
        spectrumInfos[idx] = { VK_NULL_HANDLE, m_SpectrumTexture[idx].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
        displacementInfos[idx] = { VK_NULL_HANDLE, m_DisplacementTexture[idx].imageView.handle, VK_IMAGE_LAYOUT_GENERAL };
        surfaceGradientInfos[idx] = { VK_NULL_HANDLE, m_SurfaceGradientMip0Views[idx].handle, VK_IMAGE_LAYOUT_GENERAL };
    }

    auto writes = initializers::writeDescriptorSets<5>(m_descriptorSet);
    set_buffer_write(writes[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &deformationInfo);
    set_buffer_write(writes[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, &simulationInfo);

    set_image_write(writes[2], 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, spectrumInfos.data(), g_WaterSimBandCount);
    set_image_write(writes[3], 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, displacementInfos.data(), g_WaterSimBandCount);
    set_image_write(writes[4], 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, surfaceGradientInfos.data(), g_WaterSimBandCount);

    m_device->updateDescriptorSets(writes);
}
