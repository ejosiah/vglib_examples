#ifndef ATMOSPHERE_MODEL_GLSL
#define ATMOSPHERE_MODEL_GLSL

#include "atmosphere_meta_data.glsl"

layout(set = 0, binding = 0) uniform UBO {
    vec3 solarIrradiance;
    vec3 rayleighScattering;
    vec3 mieScattering;
    vec3 mieExtinction;
    vec3 absorptionExtinction;
    vec3 groundAlbedo;
    float sunAngularRadius;
    float bottomRadius;
    float topRadius;
    float mu_s_min;
    float lengthUnitInMeters;
    float mieAnisotropicFactor;
} ubo;


layout(set = 0, binding = 1) buffer DENSITY_PROFILES {
    DensityProfileLayer v;
} density_profiles[NUM_DENSITY_PROFILES];


AtmosphereParameters ATMOSPHERE = AtmosphereParameters(
    ubo.solarIrradiance,
    ubo.sunAngularRadius,
    ubo.bottomRadius,
    ubo.topRadius,
    DensityProfile(
        DensityProfileLayer[2](
            DensityProfileLayer(0.000000, 0.000000, 0.000000, 0.000000, 0.000000),
            density_profiles[DENSITY_PROFILE_RAYLEIGH].v
        )
    ),
    ubo.rayleighScattering,
    DensityProfile(
        DensityProfileLayer[2](
            DensityProfileLayer(0.000000, 0.000000, 0.000000, 0.000000, 0.000000),
            density_profiles[DENSITY_PROFILE_MIE].v
        )
    ),
    ubo.mieExtinction,
    ubo.mieExtinction,
    ubo.mieAnisotropicFactor,
    DensityProfile(
        DensityProfileLayer[2](
            density_profiles[DENSITY_PROFILE_OZONE_BOTTOM].v,
            density_profiles[DENSITY_PROFILE_OZONE_TOP].v
        )
    ),
    ubo.absorptionExtinction,
    ubo.groundAlbedo,
    ubo.mu_s_min
);

const vec3 SKY_SPECTRAL_RADIANCE_TO_LUMINANCE = vec3(114974.916437, 71305.954816, 65310.548555);
const vec3 SUN_SPECTRAL_RADIANCE_TO_LUMINANCE = vec3(98242.786222, 69954.398112, 66475.012354);

#endif// ATMOSPHERE_MODEL_GLSL