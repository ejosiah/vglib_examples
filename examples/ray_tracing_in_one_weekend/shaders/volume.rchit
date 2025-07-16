#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"

layout(buffer_reference, buffer_reference_align=8) buffer MediumBuffer {
    Medium at[];
};

layout(shaderRecord, std430) buffer SBT {
    MediumBuffer mediums;
};


layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

void main() {

    vec3 p, n;
    getSurfaceInfo(bc, gl_HitTriangleVertexPositionsEXT, gl_ObjectToWorld, p, n);

    if(gl_HitKind == gl_HitKindFrontFacingTriangle && !hRec.inside_medium) {
        hRec.x = p;
        hRec.color = vec3(0);
        hRec.emission = vec3(0);
        hRec.attenuation = vec3(1);
        hRec.inside_medium = true;
        return;
    }

    Medium medium = mediums.at[gl_InstanceCustomIndex];
    float boundary_dist = distance(gl_WorldRayOrigin, p);
    float hit_dist = -(1/medium.density) * log(sampleReal(hRec));

    if(hit_dist < boundary_dist) {
        hRec.x = p + hit_dist * normalize(gl_WorldRayDirection);
        vec3 n = vec3(1, 0, 0);
        vec3 wi = uniformSampleSphere(sampleVec2(hRec));
        vec3 tn, bn;
        othonormalBasis(tn, bn, n);
        mat3 TBN = mat3(tn, bn, n);
        hRec.wi = TBN * wi;
        hRec.attenuation = medium.albedo;
    } else {
        hRec.x = p;
        hRec.bounce = -1; // skip this hit
    }
    hRec.inside_medium = false;
}