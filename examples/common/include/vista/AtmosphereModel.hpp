#pragma once

#include "Texture.h"
#include "ContextAware.hpp"
#include "ComputePipelins.hpp"

#include "atmosphere/Atmosphere.hpp"
#include <array>

class AtmosphereModel : public ContextAware {
public:
    struct Descriptor {
        VulkanDescriptorSetLayout setLayout;
        VkDescriptorSet set{};
    };

    AtmosphereModel(Context& context);

    void init();

    void newFrame();

    void preProcess(VkCommandBuffer commandBuffer);

    void renderSkyView(VkCommandBuffer commandBuffer);

    void renderArealPerspective(VkCommandBuffer commandBuffer);

    void controls(bool show = true);

    bool arealPerspectiveEnabled() const;

    Descriptor descriptor() const;

    float printPerfStats();

protected:
    Context& context() final;

    void initQueries();

    void computeTransmittanceLUT(VkCommandBuffer commandBuffer);

    void computeMultipleScatteringLUT(VkCommandBuffer commandBuffer);

    void computeSkyViewLUT(VkCommandBuffer commandBuffer);

    void computeArealPerspectiveLut(VkCommandBuffer commandBuffer);

    void prepareForWriting(VkCommandBuffer commandBuffer, const VulkanImage& image);

    void prepareForReading(VkCommandBuffer commandBuffer, const VulkanImage& image);

    void initUniforms();

    void createDescriptorSetLayout();

    void updateDescriptorSet();

    void createLoopUpTextures();

    void createComputePipelines();

    void createRenderPipelines();

    std::vector<PipelineMetaData> metadata();

private:

    static constexpr uint BOTTOM = 0;
    static constexpr uint TOP = 1;

    static constexpr uint TRANSMITTANCE_TEXTURE_WIDTH = 256;
    static constexpr uint TRANSMITTANCE_TEXTURE_HEIGHT = 64;

    static constexpr uint MULTI_SCATTERING_TEXTURE_WIDTH = 32;
    static constexpr uint MULTI_SCATTERING_TEXTURE_HEIGHT = 32;

    static constexpr uint SKY_VIEW_TEXTURE_WIDTH = 192;
    static constexpr uint SKY_VIEW_TEXTURE_HEIGHT = 128;

    static constexpr uint AREAL_PERSPECTIVE_TEXTURE_WIDTH = 32;
    static constexpr uint AREAL_PERSPECTIVE_TEXTURE_HEIGHT = 32;
    static constexpr uint AREAL_PERSPECTIVE_TEXTURE_DEPTH = 32;

    Context* m_context{};
    ComputePipelines m_compute;

     struct DensityProfileLayer {
        float width{};
        float exp_term{};
        float exp_scale{};
        float linear_term{};
        float constant_term{};
    };

    struct UniformData {
        glm::mat4 inverseProjection;
        glm::mat4 inverseView;
        std::array<DensityProfileLayer, 2> rayleighDensity;
        std::array<DensityProfileLayer, 2> mieDensity;
        std::array<DensityProfileLayer, 2> ozone;
        glm::vec3 solarIrradiance;
        glm::vec3 ozoneExtinction;
        glm::vec3 rayleighScattering;
        glm::vec3 mieScattering;
        glm::vec3 mieExtinction;
        glm::vec3 mieAbsorption;
        glm::vec3 groundAlbedo;
        glm::vec3 sunDirection;
        glm::vec3 cameraPosition;
        float mieAnisotropicFactor;
        float bottomRadius;
        float topRadius;
        float sunAngularRadius;
        float sunPhiAngle;
        float sunThetaAngle;
        float mu_s_min;
        float lengthUnitInMeters;
        uint transmittanceTextureIndex{~0u};
        uint multiScatteringTextureIndex{~0u};
        uint skyViewTextureIndex{~0u};
        uint arealPerspectiveTextureIndex{~0u};
        uint transmittanceImageIndex{~0u};
        uint multiScatteringImageIndex{~0u};
        uint skyViewImageIndex{~0u};
        uint arealPerspectiveImageIndex{~0u};
    };

    void set(UniformData& uniforms);


    struct {
        VulkanBuffer gpu;
        UniformData* cpu{};
    } m_uniforms;

    struct {
        Texture transmittance;
        Texture multiScattering;
        Texture skyView;
        Texture arealPerspective;
    } m_lut;

    Atmosphere::Params params{};
    bool m_arealPerspectiveEnabled{true};

    Descriptor m_descriptor;
    std::array<VkDescriptorSet, 2> m_sets;
    struct {
        Pipeline skyView;
        Pipeline arealPerspective;
    } m_render;

    static constexpr int QUERY_TRANSMISSION_LUT_ID = 0;
    static constexpr int QUERY_MULTIPLE_SCATTERING_LUT_ID = 1;
    static constexpr int QUERY_SKY_VIEW_LUT_ID = 2;
    static constexpr int QUERY_AREAL_PERSPECTIVE_LUT_ID = 3;
    static constexpr int QUERY_SKY_VIEW_RENDER_ID = 4;
    static constexpr int QUERY_AREAL_PERSPECTIVE_RENDER_ID = 5;
    std::vector<std::string> queryIds{ "transmission lut", "scattering Lut", "sky view lut", "areal p lut", "render sky view", "render areal p" };
};
