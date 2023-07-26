#pragma once
#include "Texture.h"
#include "common.h"
#include <glm/glm.hpp>
#include <array>
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanDescriptorSet.h"
#include "filemanager.hpp"
#include <functional>

class Atmosphere{
public:
    static constexpr int TRANSMITTANCE_TEXTURE_WIDTH = 256;
    static constexpr int TRANSMITTANCE_TEXTURE_HEIGHT = 64;
    static constexpr int SCATTERING_TEXTURE_R_SIZE = 32;
    static constexpr int SCATTERING_TEXTURE_MU_SIZE = 128;
    static constexpr int SCATTERING_TEXTURE_MU_S_SIZE = 32;
    static constexpr int SCATTERING_TEXTURE_NU_SIZE = 8;
    static constexpr int IRRADIANCE_TEXTURE_WIDTH = 64;
    static constexpr int IRRADIANCE_TEXTURE_HEIGHT = 16;
    static constexpr float MAX_SUN_ZENITH_ANGLE = glm::radians(120.f);
    static constexpr int DENSITY_PROFILE_RAYLEIGH = 0;
    static constexpr int DENSITY_PROFILE_MIE = 1;
    static constexpr int DENSITY_PROFILE_OZONE = 2;
    static constexpr int NUM_DENSITY_PROFILES  = 4;
    static constexpr int BOTTOM = 0;
    static constexpr int TOP = 1;

    static constexpr int SCATTERING_TEXTURE_WIDTH = SCATTERING_TEXTURE_NU_SIZE * SCATTERING_TEXTURE_MU_S_SIZE;
    static constexpr int SCATTERING_TEXTURE_HEIGHT = SCATTERING_TEXTURE_MU_SIZE;
    static constexpr int SCATTERING_TEXTURE_DEPTH = SCATTERING_TEXTURE_R_SIZE;

    struct Params{
        glm::vec3 solarIrradiance{1.474000, 1.850400, 1.911980};
        float sunAngularRadius{0.004675};

        struct {
            float bottom{6360 * km};
            float top{6460 * km};
        } radius;

        struct {
            glm::vec3 scattering{0.005802/km, 0.013558/km, 0.033100/km};
            float height{8 * km};
        } rayleigh;

        struct {
            glm::vec3 scattering{0.003996f/km};
            glm::vec3 extinction{0.004440/km};
            float height{1.2 * km};
            float anisotropicFactor{0.8};
        } mie;

        struct {
            struct {
                float width{25 * km};
                float linearHeight{15 * km};
                float constant{-2.0/3.0};
            } bottom;
            struct {
                float linearHeight{15 * km};
                float constant{8.0/3.0};
            } top;
            glm::vec3  absorptionExtinction{0.000650/km,0.001881/km,0.000085/km};
        } ozone;

        glm::vec3 groundAlbedo{0.1};
        float mu_s_min{glm::cos(MAX_SUN_ZENITH_ANGLE)};
        int numScatteringOrder{NUM_SCATTERING_ORDER};
        float lengthUnitInMeters{1 * km};
    };


     struct alignas(16) DensityProfileLayer {
        float width;
        float exp_term;
        float exp_scale;
        float linear_term;
        float constant_term;
    };

    Atmosphere(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, FileManager* fileManager);

    void generateLUT();

    static std::function<void()> ui(Atmosphere& atmosphere);

protected:
    void createBuffers();

    void createBarriers();

    void createSampler();

    void createLutTextures();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

    void computeTransmittanceLUT(VkCommandBuffer commandBuffer);

    void computeDirectIrradiance(VkCommandBuffer commandBuffer);

    void computeSingleScattering(VkCommandBuffer commandBuffer);

    void computeScatteringDensity(VkCommandBuffer commandBuffer, int scatteringOrder);

    void computeIndirectIrradiance(VkCommandBuffer commandBuffer, int scatteringOrder);

    void computeMultipleScattering(VkCommandBuffer commandBuffer);

    void barrier(VkCommandBuffer commandBuffer, std::vector<int> images);

    void refresh();

public:
    Params params;

    Texture transmittanceLUT;
    Texture irradianceLut;
    Texture scatteringLUT;

    VulkanDescriptorSetLayout uboDescriptorSetLayout;
    VkDescriptorSet uboDescriptorSet;

    VulkanDescriptorSetLayout  imageDescriptorSetLayout;
    VkDescriptorSet  imageDescriptorSet;

    VulkanDescriptorSetLayout lutDescriptorSetLayout;
    VkDescriptorSet lutDescriptorSet;

private:
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

    UBO* ubo;
    DensityProfileLayer* layers;
    VulkanBuffer m_densityProfileBuffer;
    VulkanBuffer m_uboBuffer;
    VulkanDevice* m_device;
    VulkanDescriptorPool* m_descriptorPool;
    FileManager* m_fileMgr;
    VulkanSampler sampler;

    struct Pipeline{
        VulkanPipeline pipeline;
        VulkanPipelineLayout layout;
    };

    Texture deltaIrradianceTexture;
    Texture deltaRayleighScatteringTexture;
    Texture deltaMieScatteringTexture;
    Texture deltaScatteringDensityTexture;
    Texture deltaMultipleScatteringTexture;

    VulkanDescriptorSetLayout tempDescriptorSetLayout;
    VkDescriptorSet tempDescriptorSet;

    struct {
        Pipeline compute_transmittance;
        Pipeline compute_direct_irradiance;
        Pipeline compute_single_scattering;
        Pipeline compute_scattering_density;
        Pipeline compute_indirect_irradiance;
        Pipeline compute_multiple_scattering;
    } pipelines;

    std::vector<VkImageMemoryBarrier> m_barriers{};
    static constexpr int TRANSMITTANCE_BARRIER = 0;
    static constexpr int IRRADIANCE_BARRIER = 1;
    static constexpr int DELTA_RAYLEIGH_BARRIER = 2;
    static constexpr int DELTA_MIE_BARRIER = 3;
    static constexpr int DELTA_IRRADIANCE_BARRIER = 4;
    static constexpr int DELTA_SCATTERING_DENSITY_BARRIER = 5;
    static constexpr int DELTA_MULTIPLE_DENSITY_BARRIER = 6;
    static constexpr int SCATTERING_BARRIER = 7;
    static constexpr int NUM_BARRIERS = 8;

    static constexpr int NUM_SCATTERING_ORDER = 4;
};