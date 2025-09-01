#ifndef ATMOSPHERE_UNIFORM_GLSL
#define ATMOSPHERE_UNIFORM_GLSL

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#include "bruneton/definitions.glsl"

#ifndef ATMOSPHERE_UNIFORM_SET
#define ATMOSPHERE_UNIFORM_SET 0
#ifndef ATMOSPHERE_UNIFORM_BINDING
#define ATMOSPHERE_UNIFORM_BINDING 0
#endif // ATMOSPHERE_UNIFORM_BINDING
#endif // ATMOSPHERE_UNIFORM_SET

layout(set = ATMOSPHERE_UNIFORM_SET, binding = ATMOSPHERE_UNIFORM_BINDING, scalar) uniform AtmosphereParams {
    mat4 inverseViewProjection;
    DensityProfileLayer rayleighDensityBottom;
    DensityProfileLayer rayleighDensityTop;
    DensityProfileLayer mieDensityBottom;
    DensityProfileLayer mieDensityTop;
    DensityProfileLayer ozoneDensityBottom;
    DensityProfileLayer ozoneDensityTop;
    vec3 solarIrradiance;
    vec3 ozoneExtinction;
    vec3 rayleighScattering;
    vec3 mieScattering;
    vec3 mieExtinction;
    vec3 mieAbsorption;
    vec3 groundAlbedo;
    vec3 sunDirection;
    vec3 cameraPosition;
    float mieAnisotropicFactor;
    float bottomRadius;
    float topRadius;
    float sunAngularRadius;
    float sunPhiAngle;
    float sunThetaAngle;
    float mu_s_min;
    float lengthUnitInMeters;
    uint transmittanceTextureIndex;
    uint multiScatteringTextureIndex;
    uint skyViewTextureIndex;
    uint arealPerspectiveTextureIndex;
    uint transmittanceImageIndex;
    uint multiScatteringImageIndex;
    uint skyViewImageIndex;
    uint arealPerspectiveImageIndex;
} atm;

AtmosphereParameters ATMOSPHERE = AtmosphereParameters(
    atm.solarIrradiance,
    atm.sunAngularRadius,
    atm.bottomRadius,
    atm.topRadius,
    DensityProfile(DensityProfileLayer[2](atm.rayleighDensityTop, atm.rayleighDensityBottom)),
    atm.rayleighScattering,
    DensityProfile(DensityProfileLayer[2](atm.mieDensityTop, atm.mieDensityBottom)),
    atm.mieExtinction,
    atm.mieExtinction,
    atm.mieAnisotropicFactor,
    DensityProfile(DensityProfileLayer[2](atm.ozoneDensityBottom, atm.ozoneDensityTop)),
    atm.ozoneExtinction,
    atm.groundAlbedo,
    atm.mu_s_min
);


AtmosphereParameters GetAtmosphereParameters() {
    return AtmosphereParameters(
        atm.solarIrradiance,
        atm.sunAngularRadius,
        atm.bottomRadius,
        atm.topRadius,
        DensityProfile(DensityProfileLayer[2](atm.rayleighDensityTop, atm.rayleighDensityBottom)),
        atm.rayleighScattering,
        DensityProfile(DensityProfileLayer[2](atm.mieDensityTop, atm.mieDensityBottom)),
        atm.mieExtinction,
        atm.mieExtinction,
        atm.mieAnisotropicFactor,
        DensityProfile(DensityProfileLayer[2](atm.ozoneDensityBottom, atm.ozoneDensityTop)),
        atm.ozoneExtinction,
        atm.groundAlbedo,
        atm.mu_s_min
    );
}

#define transmittanceLUT global_textures[nonUniformEXT(atm.transmittanceTextureIndex)]
#define multiscatteringLUT global_textures[nonUniformEXT(atm.multiScatteringTextureIndex)]
#define skyViewLUT global_textures[nonUniformEXT(atm.skyViewTextureIndex)]
#define ArealPerspective global_textures_3d[nonUniformEXT(atm.arealPerspectiveTextureIndex)]

#endif // ATMOSPHERE_UNIFORM_GLSL