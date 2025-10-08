#version 460

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#define ATMOSPHERE_LUT_SET 2
#include "atmosphere/bruneton_api.glsl"

layout(set = 3, binding = 1, input_attachment_index = 1) uniform subpassInput positionInput;

layout(set = 0, binding = 0) uniform Info {
    mat4 inverseModel;
    mat4 inverseView;
    mat4 inverseProjection;

    vec4 camera;
    vec4 earthCenter;
    vec4 sunDirection;
    vec4 whitePoint;
    vec2 sunSize;
    float exposure;
} u;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 scatterTransmission;
layout(location = 1) out vec4 extras;

void main() {
    float d = subpassLoad(positionInput).x;
    vec4 clipPos = vec4(uv * 2 - 1, d, 1);
    vec4 viewPos = u.inverseProjection * clipPos;
    viewPos /= viewPos.w;

    vec3 point = (u.inverseView * viewPos).xyz - u.earthCenter.xyz;

    vec3 camera = u.camera.xyz - u.earthCenter.xyz;
    vec3 viewDir = point - camera;
    float depth = length(viewDir);
    viewDir /= depth;

    vec3 transmittance;
    vec3 inScatter = GetSkyRadianceToPoint(camera, point, 0, u.sunDirection.xyz, transmittance);
    inScatter *= 10;

    scatterTransmission = vec4(inScatter, transmittance);
}