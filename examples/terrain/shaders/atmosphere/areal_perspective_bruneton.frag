#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];

#define RADIANCE_API_ENABLED
#include "atm_uniforms.glsl"
#include "bruneton_common.glsl"

#define radianceInput global_textures[nonuniformEXT(atm.radianceTextureIndex)]
#define positionInput global_textures[nonuniformEXT(atm.positionTextureIndex)]
#define depthInput global_textures[nonuniformEXT(atm.depthTextureIndex)]

layout(location = 0) in vec2 uv;

layout(location = 0) out vec3 radiance;

const float tMax = 100000;  // TODO use far plane distance

void main() {
    vec4 clipPos = vec4(2 * uv - 1, 1, 1);
    vec4 viewPos = atm.inverseProjection * clipPos;
    viewPos /= viewPos.w;
    vec3 earthCenter = vec3(0, -ATMOSPHERE.bottom_radius, 0);

    vec3 camera = localUnitsToAtmosphere(atm.cameraPosition) - earthCenter;
    float shadow_length = 0;
    float scatterFactor = 1.0;

    vec3 sunDirection = atm.sunDirection;
    radiance = texture(radianceInput, uv).rgb;
    float depth = texture(depthInput, uv).x;
//    radiance = vec3(linearizeDepth(depth));
    if(depth < 1) {
        vec3 worldPos = texture(positionInput, uv).xyz;
        vec3 point = localUnitsToAtmosphere(worldPos) - earthCenter;

        vec3 transmittance;
        vec3 in_scatter = GetSkyRadianceToPoint(camera, point, shadow_length, sunDirection, transmittance) * scatterFactor;
        radiance = radiance * transmittance + in_scatter;
    }else {
        vec3 cameraDir = normalize((atm.inverseView * vec4(viewPos.xyz, 0)).xyz);
        vec3 point = camera + cameraDir * tMax;

        vec3 transmittance;
        vec3 in_scatter = GetSkyRadianceToPoint(camera, point, shadow_length, sunDirection, transmittance) * scatterFactor;
        radiance = radiance * transmittance + in_scatter;
    }
//    vec3 whitePoint = vec3(1);
//    float exposure = atm.exposure;
//    vec3 toneMapped = pow(vec3(1.0) - exp(-radiance / whitePoint * exposure), vec3(1.0 / 2.2));
//    vec3 toneMapped = tone_map(radiance, ACES);
//    toneMapped = pow(toneMapped, vec3(0.454));
//    radiance = toneMapped;
//    gl_FragDepth = depth;
}