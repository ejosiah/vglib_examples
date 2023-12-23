#ifndef ATMOSPHERE_DESCRIPTOR
#define ATMOSPHERE_DESCRIPTOR

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

layout(set = 1, binding = 0) uniform sampler2D irradiance_texture;
layout(set = 1, binding = 1) uniform sampler2D transmittance_texture;
layout(set = 1, binding = 2) uniform sampler3D scattering_texture;
layout(set = 1, binding = 3) uniform sampler3D single_mie_scattering_texture;

#endif // ATMOSPHERE_DESCRIPTOR