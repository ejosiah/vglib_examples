#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

struct Metal {
    vec3 albedo;
    float fuzz;
};

layout(buffer_reference, buffer_reference_align=8) buffer MetalBuffer {
    Metal at[];
};

layout(buffer_reference, buffer_reference_align=8) buffer SphereBuffer {
    Sphere at[];
};

layout(shaderRecord, std430) buffer SBT {
    SphereBuffer spheres;
    MetalBuffer metals;
};

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

float u = 1 - bc.x - bc.y;
float v = bc.x;
float w = bc.y;

void main() {

    vec3 p, n;
    getSurfaceInfo(spheres.at[gl_PrimitiveID], gl_WorldRayOrigin, gl_WorldRayDirection, gl_HitT, p, n);
    hRec.n = n;
    hRec.x = p;
    float fuzz = metals.at[gl_PrimitiveID].fuzz;
    hRec.wi = reflect(gl_WorldRayDirection, n) + fuzz * uniformSampleSphere(sampleVec2(hRec.rngState));
    hRec.wi *= sign(max(0, dot(n, hRec.wi)));
    hRec.attenuation = metals.at[gl_PrimitiveID].albedo;
}