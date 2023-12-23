#ifndef ATMOSPHERE_LUT_GLSL
#define ATMOSPHERE_LUT_GLSL

#include "atmosphere_lib.glsl"

layout(set = 1, binding = 0) uniform sampler2D irradiance_texture;
layout(set = 1, binding = 1) uniform sampler2D transmittance_texture;
layout(set = 1, binding = 2) uniform sampler3D scattering_texture;
layout(set = 1, binding = 3) uniform sampler3D single_mie_scattering_texture;

#ifdef RADIANCE_API_ENABLED
RadianceSpectrum GetSolarRadiance() {
    return ATMOSPHERE.solar_irradiance /
    (PI * ATMOSPHERE.sun_angular_radius * ATMOSPHERE.sun_angular_radius);
}
RadianceSpectrum GetSkyRadiance(Position camera, Direction view_ray, Length shadow_length, Direction sun_direction, out DimensionlessSpectrum transmittance) {
    camera /= ubo.lengthUnitInMeters;
    shadow_length /= ubo.lengthUnitInMeters;
    return GetSkyRadiance(ATMOSPHERE, transmittance_texture,scattering_texture, single_mie_scattering_texture, camera, view_ray, shadow_length, sun_direction, transmittance);
}

RadianceSpectrum GetSkyRadianceToPoint(Position camera, Position point, Length shadow_length, Direction sun_direction, out DimensionlessSpectrum transmittance) {
    camera /= ubo.lengthUnitInMeters;
    point /= ubo.lengthUnitInMeters;
    shadow_length /= ubo.lengthUnitInMeters;
    return GetSkyRadianceToPoint(ATMOSPHERE, transmittance_texture,scattering_texture, single_mie_scattering_texture, camera, point, shadow_length, sun_direction, transmittance);
}

IrradianceSpectrum GetSunAndSkyIrradiance(Position p, Direction normal, Direction sun_direction, out IrradianceSpectrum sky_irradiance) {
    p /= ubo.lengthUnitInMeters;
    return GetSunAndSkyIrradiance(ATMOSPHERE, transmittance_texture, irradiance_texture, p, normal, sun_direction, sky_irradiance);
}
#endif// RADIANCE_API_ENABLED

Luminance3 GetSkyLuminance(Position camera, Direction view_ray, Length shadow_length,
                           Direction sun_direction, out DimensionlessSpectrum transmittance){

    camera /= ubo.lengthUnitInMeters;
    shadow_length /= ubo.lengthUnitInMeters;

    return
        GetSkyRadiance(ATMOSPHERE, transmittance_texture, scattering_texture, single_mie_scattering_texture,
                camera, view_ray, shadow_length, sun_direction, transmittance) * SKY_SPECTRAL_RADIANCE_TO_LUMINANCE;
}

Luminance3 GetSkyLuminanceToPoint(
    Position camera,
    Position point,
    Length shadow_length,
    Direction sun_direction,
    out DimensionlessSpectrum transmittance
) {
    camera /= ubo.lengthUnitInMeters;
    point /= ubo.lengthUnitInMeters;
    shadow_length /= ubo.lengthUnitInMeters;

    return GetSkyRadianceToPoint(ATMOSPHERE, transmittance_texture, scattering_texture, single_mie_scattering_texture,
                    camera, point, shadow_length, sun_direction, transmittance) * SKY_SPECTRAL_RADIANCE_TO_LUMINANCE;
}

Illuminance3 GetSunAndSkyIlluminance(Position p, Direction normal, Direction sun_direction, out IrradianceSpectrum sky_irradiance) {
    p /= ubo.lengthUnitInMeters;

    IrradianceSpectrum sun_irradiance =
        GetSunAndSkyIrradiance(ATMOSPHERE, transmittance_texture, irradiance_texture, p, normal, sun_direction, sky_irradiance);
    sky_irradiance *= SKY_SPECTRAL_RADIANCE_TO_LUMINANCE;
    return sun_irradiance * SUN_SPECTRAL_RADIANCE_TO_LUMINANCE;
}

#ifdef USE_LUMINANCE
#define GetSolarRadiance GetSolarLuminance
#define GetSkyRadiance GetSkyLuminance
#define GetSkyRadianceToPoint GetSkyLuminanceToPoint
#define GetSunAndSkyIrradiance GetSunAndSkyIlluminance
#endif

vec3 GetSolarRadiance();

vec3 GetSkyRadiance(vec3 camera, vec3 view_ray, float shadow_length, vec3 sun_direction, out vec3 transmittance);

vec3 GetSkyRadianceToPoint(vec3 camera, vec3 point, float shadow_length, vec3 sun_direction, out vec3 transmittance);

vec3 GetSunAndSkyIrradiance(vec3 p, vec3 normal, vec3 sun_direction, out vec3 sky_irradiance);

#endif // ATMOSPHERE_LUT_GLSL