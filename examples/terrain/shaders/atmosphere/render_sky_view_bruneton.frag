#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];

#define RADIANCE_API_ENABLED
#include "atm_uniforms.glsl"
#include "bruneton_common.glsl"

layout(early_fragment_tests) in;

layout(location = 0) in struct {
    vec3 viewDirection;
} fs_in;

layout(location = 0) out vec3 radiance;

void main() {

    float shadowLength = 0;
    float sunSize = cos(atm.sunAngularRadius);
    vec3 camera = localUnitsToAtmosphere(atm.cameraPosition) - vec3(0, -ATMOSPHERE.bottom_radius, 0);
    vec3 cameraDir = normalize(fs_in.viewDirection);
    vec3 sunDirection = atm.sunDirection;

    vec3 transmittance;
    radiance = GetSkyRadiance(camera , cameraDir, shadowLength, sunDirection, transmittance);
    if (dot(cameraDir, sunDirection) > sunSize) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }

    radiance /= radiance + 1;
    radiance = pow(radiance, vec3(0.454545));
}