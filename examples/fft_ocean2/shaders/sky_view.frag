#version 460

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#define ATMOSPHERE_LUT_SET 2
#include "atmosphere/bruneton_api.glsl"

layout(early_fragment_tests) in;

layout(set = 0, binding = 0) uniform Info {
    mat4 inverse_model;
    mat4 inverse_view;
    mat4 inverse_projection;

    vec4 camera;
    vec4 earthCenter;
    vec4 sunDirection;
    vec4 whitePoint;
    vec2 sunSize;
    float exposure;
};

layout(location = 0) in vec3 viewDirection;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 extras;

void main() {
    vec3 V = normalize(viewDirection);
    V.y = max(0, V.y);
    vec3 L = normalize(sunDirection.xyz);

    vec3 cameraPos = (camera - earthCenter).xyz;
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(cameraPos, V, 0, L, transmittance);
    if (dot(V, L) > sunSize.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }

    extras.x = gl_FragCoord.z;
    fragColor = vec4(radiance, 1);
}