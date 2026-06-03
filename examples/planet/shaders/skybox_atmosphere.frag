#version 460 core

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 2
#define ATMOSPHERE_LUT_SET 3
#include "atmosphere/bruneton_api.glsl"

layout(set = 0, binding = 0) uniform sampler2D equirectangular_map;

layout(set = 1, binding = 0) uniform AtmosphereInfo {
    mat4 inverse_model;
    mat4 inverse_view;
    mat4 inverse_projection;

    vec4 camera;
    vec4 earthCenter;
    vec4 sunDirection;
    vec4 whitePoint;
    vec2 sunSize;
    float exposure;
} atmosphereInfo;

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 fragColor;

vec2 dirToEquirectUV(vec3 dir) {
    dir = normalize(dir);

    float u = atan(dir.z, dir.x) * (1.0 / (2.0 * 3.14159265359)) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) * (1.0 / 3.14159265359) + 0.5;

    return vec2(u, v);
}

vec3 tone_map(vec3 radiance) {
    vec3 whitePoint = max(atmosphereInfo.whitePoint.xyz, vec3(1e-3));
    vec3 mapped = vec3(1.0) - exp(-max(radiance, vec3(0.0)) / whitePoint * atmosphereInfo.exposure);
    return pow(max(mapped, vec3(0.0)), vec3(1.0 / 2.2));
}

void main() {
    vec3 direction = normalize(texCoord);
    vec3 milkyway = texture(equirectangular_map, dirToEquirectUV(direction)).rgb;

    vec3 sun = normalize(atmosphereInfo.sunDirection.xyz);
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance((atmosphereInfo.camera - atmosphereInfo.earthCenter).xyz, direction, 0.0, sun, transmittance);
    if (dot(direction, sun) > atmosphereInfo.sunSize.y) {
        radiance += transmittance * GetSolarRadiance();
    }

    vec3 atmosphere = tone_map(radiance);
    vec3 color = milkyway * transmittance + atmosphere;
    fragColor = vec4(color, 1.0);
}
