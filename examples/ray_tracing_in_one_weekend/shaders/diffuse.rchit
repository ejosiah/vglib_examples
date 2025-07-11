#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(buffer_reference, buffer_reference_align=8) buffer SphereBuffer {
    Sphere at[];
};

layout(buffer_reference, buffer_reference_align=8) buffer MaterialBuffer {
    vec4 at[];
};

layout(shaderRecord, std430) buffer SBT {
    SphereBuffer spheres;
    MaterialBuffer materials;
};


layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

void main() {

    vec3 p, n;
    getSurfaceInfo(spheres.at[gl_PrimitiveID], gl_WorldRayOrigin, gl_WorldRayDirection, gl_HitT, p, n);

    hRec.n = n;
    hRec.x = p;
    hRec.wi = hRec.x + n + uniformSampleSphere(sampleNoiseBlue(hRec.seed));
    hRec.attenuation = materials.at[gl_PrimitiveID].rgb;
}