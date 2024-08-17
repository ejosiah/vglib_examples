#version 460

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#define ATMOSPHERE_LUT_SET 2
#include "atmosphere_api.glsl"

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

layout(location = 0) in struct {
    vec3 position;
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    fragColor = vec4(fs_in.direction, 1);

    vec3 direction = normalize(fs_in.direction);
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance((camera - earthCenter).xyz, direction, 0, sunDirection.xyz, transmittance);
    if (dot(direction, sunDirection.xyz) > sunSize.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / whitePoint.xyz * exposure), vec3(1.0 / 2.2));
}