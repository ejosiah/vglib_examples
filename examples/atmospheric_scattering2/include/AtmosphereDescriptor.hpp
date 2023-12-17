#pragma once

#include "Atmosphere.hpp"
#include "Texture.h"

#include <vulkan/vulkan.h>

struct AtmosphereDescriptor {

    struct UBO {
        alignas(16) glm::vec3 solarIrradiance;
        alignas(16) glm::vec3 rayleighScattering;
        alignas(16) glm::vec3 mieScattering;
        alignas(16) glm::vec3 mieExtinction;
        alignas(16) glm::vec3 absorptionExtinction;
        alignas(16) glm::vec3 groundAlbedo;
        float sunAngularRadius;
        float bottomRadius;
        float topRadius;
        float mu_s_min;
        float lengthUnitInMeters;
        float mieAnisotropicFactor;
    };

    Texture transmittanceLUT;
    Texture irradianceLut;
    Texture scatteringLUT;

    VulkanDescriptorSetLayout uboDescriptorSetLayout;
    VkDescriptorSet uboDescriptorSet;

    VulkanDescriptorSetLayout lutDescriptorSetLayout;
    VkDescriptorSet lutDescriptorSet;

    VulkanSampler sampler;

    UBO* ubo;
    Atmosphere::DensityProfileLayer* layers;

    explicit AtmosphereDescriptor(VulkanDevice* device = nullptr, VulkanDescriptorPool* m_descriptorPool = nullptr);

    void init();

    void load(const std::filesystem::path& path);

private:

    void createBuffers();

    void createSampler();

    void createLutTextures();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    VulkanDevice* m_device{};
    VulkanDescriptorPool* m_descriptorPool{};
    std::filesystem::path m_path;
    VulkanBuffer m_densityProfileBuffer;
    VulkanBuffer m_uboBuffer;

};