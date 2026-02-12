#version 460

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout: enable

#include "octahedral.glsl"
#include "camera_uniform.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout (location = 0) in vec2 uv;
layout (location = 1) flat in uint texture_id;

layout(location = 0) out vec4 fragColor;

vec3 reconstructViewPos(vec2 uv, float depth) {
    vec4 ndc = vec4(2 * uv - 1, depth, 1);
    vec4 viewPos = camera.inverseProjection * ndc;
    return viewPos.xyz / viewPos.w;
}

void main() {
    vec2 texel = 1.0 / camera.viewportSize;

    float dC = texture(global_textures[nonuniformEXT(texture_id)], uv).r;
    float dR = texture(global_textures[nonuniformEXT(texture_id)], uv + vec2(texel.x, 0.0)).r;
    float dU = texture(global_textures[nonuniformEXT(texture_id)], uv + vec2(0.0, texel.y)).r;

    vec3 pC = reconstructViewPos(uv, dC);
    vec3 pR = reconstructViewPos(uv + vec2(texel.x, 0.0), dR);
    vec3 pU = reconstructViewPos(uv + vec2(0.0, texel.y), dU);

    vec3 dx = pR - pC;
    vec3 dy = pU - pC;

    vec3 viewNormal = normalize(cross(dx, dy));

    // reprojection
    float rawDepth = texture(global_textures[nonuniformEXT(texture_id)], uv).r;
    vec4 currentNdc = vec4(2 * uv - 1, rawDepth, 1);
    vec4 viewPos = camera.inverseProjection * currentNdc;
    viewPos.xyz /= viewPos.w;
    vec4 currentWorldPos = camera.inverseView * vec4(viewPos.xyz, 1);

    vec4 previousNdc = camera.previousViewProjection * currentWorldPos;
    previousNdc /= previousNdc.w;

    float depth_diff = abs( 1.0 - ( previousNdc.z / currentNdc.z ) );

    float c1 = 0.003;
    float c2 = 0.017;
    float eps = c1 + c2 * abs( viewNormal.z );

    vec2 visibility_motion = depth_diff < eps ? currentNdc.xy - previousNdc.xy  : vec2(-1);

    float ldepth = length(viewPos)/(camera.far - camera.near);
    fragColor = vec4(visibility_motion, ldepth, 1.0);

}