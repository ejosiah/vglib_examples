#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_buffer_reference : enable

#include "ray_tracing_lang.glsl"
#include "constants.glsl"
#include "common.glsl"
#include "sampling.glsl"
#include "random.glsl"
#include "quaternion.glsl"
#include "fresnel.glsl"
#include "brdf.glsl"
#include "scene.glsl"
#include "medium.glsl"

layout(set = 0, binding = 0) uniform accelerationStructure topLevelAs;

layout(set = 4, binding = 0) uniform sampler2D environmentMap;
layout(set = 4, binding = 1) uniform sampler2D pConditionalVFunc;
layout(set = 4, binding = 2) uniform sampler2D pConditionalVCdf;
layout(set = 4, binding = 3) uniform sampler1D pMarginal;
layout(set = 4, binding = 4) uniform sampler1D pMarginalCdf;

layout(buffer_reference, buffer_reference_align=8) buffer MediumBuffer{
    Medium at[];
};
layout(shaderRecord, std430) buffer SBT {
    MediumBuffer mediums;
};


#include "scene_push_constants.glsl"
#include "eval_brdf.glsl"
#include "lights.glsl"

layout(location = 0) rayPayloadIn HitData hitData;

hitAttribute vec2 attribs;

void main(){
    vec3 wo = -gl_WorldRayDirection;
    float g = mediums.at[hitData.ray.medium].g;
    hitData.lightContribution = evalLightContributionWithTranmission(hitData, wo, g);
}