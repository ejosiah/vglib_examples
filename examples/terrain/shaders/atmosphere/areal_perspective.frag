#version 460 core

#include "atm_uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures_3d[];
layout(set = 1, binding = 11) uniform writeonly image2D global_images[];
layout(set = 1, binding = 11) uniform writeonly image3D global_images_3d[];

layout(set = 2, binding = 1, input_attachment_index = 1) uniform subpassInput positionInput;

#include "common.glsl"

#define AP_SLICE_COUNT AREAL_PERSPECTIVE_TEXTURE_SIZE_F.z

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 scatterTransmission;

void main() {
    vec3 sunDirection = atm.sunDirection;
    AtmosphereParameters Atmosphere = GetAtmosphereParameters();


    float d = subpassLoad(positionInput).w;
    vec4 clipPos = vec4(uv * 2 - 1, d, 1);
    vec4 viewPos = atm.inverseProjection * clipPos;
    viewPos /= viewPos.w;

    vec3 earthCenter = vec3(0, -Atmosphere.bottom_radius, 0);
    vec3 worldPos = localUnitsToAtmosphere((atm.inverseView * viewPos).xyz) - earthCenter;

    vec3 camera = localUnitsToAtmosphere(atm.cameraPosition) - earthCenter;
    vec3 viewDir = worldPos - camera;
    float depth = length(viewDir);
    viewDir /= depth;

    float slice = AerialPerspectiveDepthToSlice(depth);
    float weight = 1.0;
    if (slice < 0.5) {
        // We multiply by weight to fade to 0 at depth 0. That works for luminance and opacity.
        weight = saturate(slice * 2.0);
        slice = 0.5;
    }
    float w = sqrt(slice / AP_SLICE_COUNT);	// squared distribution

    vec3 loc = vec3(uv, w);
    scatterTransmission = texture(ArealPerspectiveLUT, loc);

}