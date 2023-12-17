#pragma once

#include "AtmosphereContants.hpp"

#include <glm/glm.hpp>
#include <array>
#include <vulkan/vulkan.h>

#include <filesystem>

constexpr uint32_t NUM_CHANNELS = 4;

using Dim2 = glm::ivec2;
using Dim3 = glm::ivec3;

using float2 = glm::ivec2;
using float3 = glm::ivec3;

constexpr uint32_t DATA_SIZE =
        (TRANSMITTANCE_TEXTURE_WIDTH * TRANSMITTANCE_TEXTURE_HEIGHT
         + IRRADIANCE_TEXTURE_WIDTH * IRRADIANCE_TEXTURE_HEIGHT
         + SCATTERING_TEXTURE_WIDTH * SCATTERING_TEXTURE_HEIGHT * SCATTERING_TEXTURE_DEPTH)
        * sizeof(float) * NUM_CHANNELS;

namespace Atmosphere {

    struct alignas(16) DensityProfileLayer {
        float width;
        float exp_term;
        float exp_scale;
        float linear_term;
        float constant_term;
    };

    struct Header {
        Dim3 scatteringDimensions{SCATTERING_TEXTURE_WIDTH, SCATTERING_TEXTURE_HEIGHT, SCATTERING_TEXTURE_DEPTH};
        Dim2 transmittanceDimensions{TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT};
        Dim2 irradianceDimensions{IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT};

        float3 solarIrradiance;
        float3 rayleighScattering;
        float3 mieScattering;
        float3 mieExtinction;
        float3 absorptionExtinction;
        float3 groundAlbedo;
        float sunAngularRadius;
        float bottomRadius;
        float topRadius;
        float mu_s_min;
        float mieAnisotropicFactor;
        float lengthUnitInMeters;
    };


    struct Format {
        Header header;
        std::vector<char> data{};
    };

    Format load(const std::filesystem::path &path);
}