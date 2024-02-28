#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ray_tracing_lang.glsl"
#include "rt_constants.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "random.glsl"
#include "util.glsl"
#include "sampling.glsl"

layout(location = 0) rayPayloadIn PhotonGenParams pgParams;

hitAttribute vec2 attribs;

void main(){
    HitData hit = getHitData(attribs);
    pgParams.position = hit.position;

    vec3 d = hit.material.diffuse;
    float Pd = luminance(d);

    if(rand(pgParams.rngState) < Pd) { // reflect
        vec2 xi = randomVec2(pgParams.rngState);
        vec3 direction = cosineSampleHemisphere(xi);
        mat3 TBN = mat3(hit.tangent, hit.bitangent, hit.normal);

        pgParams.bounceRay.origin = offsetRay(hit.position, hit.normal);
        pgParams.bounceRay.direction = normalize(TBN * direction);
        pgParams.reflectivity = d/Pd;
        pgParams.diffuse = true;
    }else { // absorb
        pgParams.diffuse = false;
        pgParams.reflectivity = vec3(0);
    }
}