#pragma once

#include "planet.hpp"
#include "VulkanDevice.h"
#include "Texture.h"

class MoonMaterial {
public:
    MoonMaterial() = default;
    
    MoonMaterial(VulkanDevice& device);

    void initialize(const Planet& moon);

    void upload_constant_buffers();

    const VkDescriptorSet& descriptor_set() const { return m_descriptorSet; }

    static VulkanDescriptorSetLayout descriptorSetLayout;

protected:
    void prepareRendering();

    void createSamplers();

    void createBuffers();

    void loadTextures();

    void createPipelines();

    void createDescriptorSet();

    void createDescriptorSet(VulkanDevice& device, const VulkanSampler& linearRepeatSampler, const VulkanSampler& linearMirrorVSampler);

    void updateDescriptorSets();

private:
    VulkanDevice* m_device{};
    const Planet* m_moon{};
    ComputePipelines m_compute;

    Texture m_AlbedoTexture;
    Texture m_ElevationTexture;
    Texture m_ElevationSGTexture;
    Texture m_DetailTexture;
    Texture m_DetailSGTexture;
    VulkanImageView m_ElevationSGMip0View;
    VulkanImageView m_DetailSGMip0View;
    VulkanSampler m_LinearRepeatSampler;
    VulkanSampler m_LinearMirrorVSampler;

    ConstantBufferT<MoonCB> m_MoonCB;

    VkDescriptorSet m_descriptorSet{};

    // Detail properties
    float m_PatchSize{15000};
    float m_PatchAmplitude{250};
    int32_t m_NumOctaves{4};
    bool m_Attenuation{true};
};
