#pragma once
#include "Texture.h"
#include "common.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanDescriptorSet.h"
#include "filemanager.hpp"
#include "AtmosphereContants.hpp"
#include "AtmosphereDescriptor.hpp"

#include <glm/glm.hpp>

#include <array>
#include <functional>
#include <filesystem>

class AtmosphereGenerator{
public:
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


    AtmosphereGenerator(VulkanDevice* device, VulkanDescriptorPool* descriptorPool, FileManager* fileManager);

    void generateLUT();

    std::function<void()> ui();

    const AtmosphereDescriptor& atmosphereDescriptor() const;

protected:

    void initAtmosphereDescriptor();

    void createBarriers();

    void createSampler();

    void createTextures();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createPipelines();

    void computeTransmittanceLUT(VkCommandBuffer commandBuffer);

    void computeDirectIrradiance(VkCommandBuffer commandBuffer);

    void computeSingleScattering(VkCommandBuffer commandBuffer);

    void computeScatteringDensity(VkCommandBuffer commandBuffer, int scatteringOrder);

    void computeIndirectIrradiance(VkCommandBuffer commandBuffer, int scatteringOrder);

    void computeMultipleScattering(VkCommandBuffer commandBuffer);

    void barrier(VkCommandBuffer commandBuffer, const std::vector<int>& images);

    void refresh();

    void save(const std::filesystem::path& path);

    Texture& transmittanceLUT();

    Texture& irradianceLut();

    Texture& scatteringLUT();

    AtmosphereDescriptor::UBO* ubo();

    Atmosphere::DensityProfileLayer* layers();

    VulkanDescriptorSetLayout& uboDescriptorSetLayout();

    VkDescriptorSet uboDescriptorSet();

    VulkanDescriptorSetLayout& lutDescriptorSetLayout();

    VkDescriptorSet lutDescriptorSet();

public:
    Params params;

    VulkanDescriptorSetLayout  imageDescriptorSetLayout;
    VkDescriptorSet  imageDescriptorSet;

private:
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

    AtmosphereDescriptor m_atmosphereDescriptor;

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