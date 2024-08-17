#version 460

#include "scene.glsl"

layout(set = 2, binding = 10) uniform sampler2D global_textures[];
layout(set = 2, binding = 10) uniform sampler3D global_textures3d[];

#define RADIANCE_API_ENABLED
#define BIND_LESS_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#include "atmosphere_api.glsl"


layout(location = 0) in struct {
    vec3 position;
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = 1;

    fragColor = vec4(1);

    vec3 direction = normalize(fs_in.direction);
    vec3 transmittance;
    vec3 radiance = GetSkyRadiance(scene.camera - scene.earthCenter, direction, 0, scene.sunDirection, transmittance);
    if (dot(direction, scene.sunDirection) > scene.sunSize.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
}