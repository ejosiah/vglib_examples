#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "ray_tracing_lang.glsl"
#include "common.glsl"
#include "fresnel.glsl"
#include "scene.glsl"

layout(location = 1) rayPayloadIn OcclusionData ocData;

hitAttribute vec2 attribs;

void main(){
    SurfaceRef ref;
    ref.objToWorld = gl_ObjectToWorld;
    ref.attribs = attribs;
    ref.instanceId = gl_InstanceID;
    ref.vertexOffsetId = gl_InstanceCustomIndex;
    ref.primitiveId = gl_PrimitiveID;

    vec3 wo = -gl_WorldRayDirection;
    Surface surface = getSurfaceData(ref, wo);

    float eta = surface.inside ? 1.5 : 1/1.5;

    ocData.ray.origin = offsetRay(surface.x, -surface.gN);
    ocData.ray.direction = refract(-wo, surface.gN, eta);
    ocData.Continue = true;
}