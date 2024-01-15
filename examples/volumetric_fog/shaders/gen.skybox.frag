#version 460

layout(set = 1, binding = 10) uniform sampler2D gTextures[];
layout(set = 1, binding = 10) uniform sampler3D gTextures3d[];

#define BIND_LESS_ENABLED
#define RADIANCE_API_ENABLED
#include "atmosphere_api.glsl"


#define SCENE_SET 2
#include "scene.glsl"

layout(location = 0) in struct {
    vec3 camDirection;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = vec4(1);

    vec3 direction = normalize(fs_in.camDirection);
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(scene.camera - scene.earthCenter, direction, 0, scene.sunDirection, transmittance);
    if (dot(direction, scene.sunDirection) > scene.sunSize.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
}