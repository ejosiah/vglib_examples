#version 460

#define RADIANCE_API_ENABLED
#include "atmosphere_api.glsl"

layout(set = 2, binding = 0) buffer SCENE_INFO {
    vec3 sunDirection;
    vec3 sunSize;
    vec3 earthCenter;
    vec3 camera;
    float exposure;
};

layout(location = 0) in struct {
    vec3 camDirection;
} fs_in;

layout(location = 0) out vec4 fragColor;

const vec3 white_point = vec3(1);

void main() {
    fragColor = vec4(1);

    vec3 direction = normalize(fs_in.camDirection);
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(camera - earthCenter, direction, 0, sunDirection, transmittance);
    if (dot(direction, sunDirection) > sunSize.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / white_point * exposure), vec3(1.0 / 2.2));
}