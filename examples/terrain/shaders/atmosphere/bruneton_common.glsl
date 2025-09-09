#ifndef ATMOSPHERE_BRUNETON_COMMON_GLSL
#define ATMOSPHERE_BRUNETON_COMMON_GLSL

#include "atm_uniforms.glsl"

#define IRRADIANCE_TEXTURE global_textures[nonuniformEXT(atm.brunetonIrradianceTextureIndex)]
#define TRANSMITTANCE_TEXTURE global_textures[nonuniformEXT(atm.transmittanceTextureIndex)]
#define SCATTERING_TEXTURE global_textures_3d[nonuniformEXT(atm.brunetonScatteringTextureIndex)]
#define SIGNLE_MIE_SCATTERING_TEXTURE global_textures_3d[nonuniformEXT(atm.brunetonSingleScatteringTextureIndex)]

#include "bruneton/functions.glsl"

#ifdef RADIANCE_API_ENABLED
vec3 GetSolarRadiance() {
    return ATMOSPHERE.solar_irradiance /
    (PI * ATMOSPHERE.sun_angular_radius * ATMOSPHERE.sun_angular_radius);
}
vec3 GetSkyRadiance(vec3 camera, vec3 view_ray, float shadow_length, vec3 sun_direction, out vec3 transmittance) {
    return GetSkyRadiance(ATMOSPHERE, TRANSMITTANCE_TEXTURE,SCATTERING_TEXTURE, SIGNLE_MIE_SCATTERING_TEXTURE, camera, view_ray, shadow_length, sun_direction, transmittance);
}

vec3 GetSkyRadianceToPoint(vec3 camera, vec3 point, float shadow_length, vec3 sun_direction, out vec3 transmittance) {
    return GetSkyRadianceToPoint(ATMOSPHERE, TRANSMITTANCE_TEXTURE,SCATTERING_TEXTURE, SIGNLE_MIE_SCATTERING_TEXTURE, camera, point, shadow_length, sun_direction, transmittance);
}

vec3 GetSunAndSkyIrradiance(vec3 p, vec3 normal, vec3 sun_direction, out vec3 sky_irradiance) {
    return GetSunAndSkyIrradiance(ATMOSPHERE, TRANSMITTANCE_TEXTURE, IRRADIANCE_TEXTURE, p, normal, sun_direction, sky_irradiance);
}
#endif// RADIANCE_API_ENABLED

#endif // ATMOSPHERE_BRUNETON_COMMON_GLSL