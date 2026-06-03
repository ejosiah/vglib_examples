#pragma once

#include "constant_buffers.hpp"

#include <VulkanDevice.h>
#include <Texture.h>

#include <glm/glm.hpp>
#include <array>

#include "constants.hpp"


class WaterData {
public:
    WaterData() = default;

    WaterData(VulkanDevice& device);

    void initialize();

    void reset_time();

    void wind_speed(const glm::vec4 &value);

    void dir_dampener(const glm::vec4 &value);

    void choppiness(float value);

    void amplification(float value);

    const std::array<Texture, g_WaterSimBandCount>& get_spectrum_texture() const;

    const std::array<Texture, g_WaterSimBandCount>& get_displacement_texture() const;

    const std::array<Texture, g_WaterSimBandCount>& get_sg_texture() const;

    std::array<Texture, g_WaterSimBandCount>& get_sg_texture();

    WaterSimulationCB get_simulation_cb() const;

    DeformationCB get_deformation_cb() const;

    // Current time
    void update_simulation(float deltaTime);

    // Upload the constant buffers to the GPU
    void upload_constant_buffers();

    void render_ui_global();

    void render_ui_patch();

    bool valid_spectrum() const;

    void validate_spectrum();

    const VkDescriptorSet& descriptor_set() const { return m_descriptorSet; }

    static VulkanDescriptorSetLayout descriptorSetLayout;

protected:

    void createDescriptorSetLayout();

    void createDescriptorSetLayout(const VulkanDevice& device);

    void updateDescriptorSets();

private:
    VulkanDevice* m_device{};
    std::array<Texture, g_WaterSimBandCount> m_SpectrumTexture;
    std::array<Texture, g_WaterSimBandCount> m_DisplacementTexture;
    std::array<Texture, g_WaterSimBandCount> m_SurfaceGradientTexture;
    std::array<VulkanImageView, g_WaterSimBandCount> m_SurfaceGradientMip0Views;

    WaterSimulationCB m_SimulationCB;

    DeformationCB m_DeformationCB;

    bool m_Initialized{};
    glm::vec4 m_WindSpeed{ 100.0f, 70.0f, 100.0f, 30.0f };
    glm::vec4 m_InternalWindSpeed{ 0.0, 0.0, 0.0, 0.0 };
    glm::vec4 m_DirDampener{ 0.4, 0.2, 0.8, 1.0 };
    glm::vec4 m_InternalDirDampener{0.0};
    bool m_ActiveSimulation{ true };
    float m_Choppiness{ 2.7f };
    float m_Amplification{ 1.2f };
    float m_TimeMultiplier{ 3.0f };
    bool m_Attenuation{ true };
    float m_AccumulatedTime{};

    std::array<bool, 4> m_PatchFlag{ true, true, true, true };

    VkDescriptorSet m_descriptorSet{};
};
