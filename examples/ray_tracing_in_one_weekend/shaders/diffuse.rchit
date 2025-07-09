#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(buffer_reference, buffer_reference_align=8) buffer MaterialBuffer {
    vec3 albedo[];
};

layout(shaderRecord, scalar) buffer SBT {
    MaterialBuffer materials;
};

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

float u = 1 - bc.x - bc.y;
float v = bc.x;
float w = bc.y;

void main() {

    vec3 p, n;
    getSurfaceInfo(gl_WorldRayOrigin, gl_WorldRayDirection, gl_HitT, gl_ObjectToWorld, p, n);

    hRec.n = n;
    hRec.x = offsetRay(p, n);
    hRec.wi = hRec.x + n + uniformSampleSphere(sampleVec2(hRec.rngState));
    hRec.attenuation = materials.albedo[gl_InstanceCustomIndex];
}