#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"
#include "dielectric.glsl"


layout(buffer_reference, buffer_reference_align=8) buffer DielectricBuffer {
    Dielectric at[];
};

layout(shaderRecord, std430) buffer SBT {
    DielectricBuffer dielectric;
};

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

void main() {

    vec3 p, n;
    getSurfaceInfo(bc, gl_HitTriangleVertexPositionsEXT, gl_ObjectToWorld, p, n);
    compute_dielectric_bsdf(p, n, -gl_WorldRayDirection, dielectric.at[gl_InstanceCustomIndex].ior, hRec);
}