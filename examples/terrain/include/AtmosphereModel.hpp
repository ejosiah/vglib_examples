#pragma once

#include "Texture.h"
#include "ContextAware.hpp"
#include "ComputePipelins.hpp"

#include <array>

class AtmosphereModel : public ContextAware {
public:
    AtmosphereModel(Context& context);

    void init();

    void preProcess(VkCommandBuffer commandBuffer);

    void render(VkCommandBuffer commandBuffer);

    void controls();

protected:
    Context& context() final;

    void createLoopUpTextures();

    void createComputePipelines();

    void createRenderPipelines();

private:
    Context* m_context{};
    ComputePipelines m_compute;

    struct UniformData {
        std::array<float, 12> rayleighDensity[12];
        std::array<float, 12> mieDensity[12];
        std::array<float, 12> absorptionDensity[12];
        glm::vec3 solarIrradiance;
        glm::vec3 absorptionExtinction;
        glm::vec3 rayleighScattering;
        glm::vec3 mieScattering;
        glm::vec3 mieExtinction;
        glm::vec3 mieAbsorption;
        glm::vec3 groundAlbedo;
        glm::vec3 sunDirection;
        glm::vec3 cameraPosition;
        float miePhaseFunctionG;
        float bottomRadius;
        float topRadius;
        float sunAngularRadius;
        float sunPhiAngle;
        float sunThetaAngle;
        uint transmittanceTextureIndex{~0u};
        uint multiScatteringTextureIndex{~0u};
        uint skyViewTextureIndex{~0u};
        uint arealPerspectiveTextureIndex{~0u};
        uint transmittanceImageIndex{~0u};
        uint multiScatteringImageIndex{~0u};
        uint skyViewImageIndex{~0u};
        uint arealPerspectiveImageIndex{~0u};
    } initialValues{};

    struct {
        Texture transmittance;
        Texture multiScattering;
        Texture skyView;
        Texture arealPerspective;
    } m_lut;
};