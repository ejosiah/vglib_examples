#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

struct Dielectric {
    float ior;
};

layout(buffer_reference, buffer_reference_align=8) buffer DielectricBuffer {
    Dielectric at[];
};

layout(shaderRecord, std430) buffer SBT {
    DielectricBuffer dielectric;
};

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

float u = 1 - bc.x - bc.y;
float v = bc.x;
float w = bc.y;

void main() {

    vec3 p, N;
    getSurfaceInfo(bc, gl_HitTriangleVertexPositionsEXT, gl_ObjectToWorld, p, N);

    vec3 I = normalize(gl_WorldRayDirection);
    float cos0 = dot(-I, N);

    float n0 = 1; // coming from air
    float n1 = dielectric.at[gl_PrimitiveID].ior;

    float kr = fresnel(cos0, n0, n1);

    if(cos0 < 0) {
        swap(n0, n1);
        N *= -1;
    }

    vec3 wi;
    if(rand(hRec.rngState) < kr) {
        hRec.wi = reflect(I, N);
        hRec.x = offsetRayImpl(p, N);
    }else {
        hRec.wi = refract(I, N, n0/n1);
        hRec.x = offsetRayImpl(p, N);
    }

    hRec.n = N;
    hRec.attenuation = vec3(1);
}