#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(set = 3, binding = 0) buffer LIGHTS {
    Light lights[];
};

layout(set = 4, binding = 0) uniform sampler2D environmentMap;
layout(set = 4, binding = 1) uniform sampler2D pConditionalVFunc;
layout(set = 4, binding = 2) uniform sampler2D pConditionalVCdf;
layout(set = 4, binding = 3) uniform sampler1D pMarginal;
layout(set = 4, binding = 3) uniform sampler1D pMarginalCdf;

layout(push_constant) uniform SceneConstants {
    uint maxBounces;
    uint frame;
    uint currentSample;
    uint numSamples;
    int numLights;
    int adaptiveSampling;
    float worldRadius;
    float pMarginalIntegral;
};

layout(location = 1) rayPayloadIn OcclusionData occData;

void main(){
    occData.Continue = false;
    occData.isShadowed = false;
}