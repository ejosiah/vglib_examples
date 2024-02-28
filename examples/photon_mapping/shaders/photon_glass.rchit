#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require

#include "ray_tracing_lang.glsl"
#include "rt_constants.glsl"
#include "scene_objects.glsl"
#include "structs.glsl"
#include "util.glsl"

layout(location = 0) rayPayloadIn PhotonGenParams pgParams;

hitAttribute vec2 attribs;

const float air = 1.0;
const float glass = 1.5;

void main(){
    HitData hit = getHitData(attribs);
    pgParams.position = hit.position;

    vec3 N = hit.normal;
    vec3 I = normalize(gl_WorldRayDirection);

    float cos0 = dot(N, -I);
    float ni = air;
    float nt = glass;
    bool totalInternalReflection = false;

    if(cos0 < 0){ // incident ray from inside glass
        swap(ni, nt);
        N *= -1;
        float sinSqr0 = 1 - cos0 * cos0;
        float sinC0 = nt/ni;
        totalInternalReflection = sinSqr0 > sinC0 * sinC0;
    }

    if(!totalInternalReflection){
        pgParams.bounceRay.origin = offsetRay(hit.position, -N);
        pgParams.bounceRay.direction = refract(I, N, ni/nt);
    }else {
        pgParams.bounceRay.origin = offsetRay(hit.position, N);
        pgParams.bounceRay.direction = reflect(I, N);
    }

    pgParams.reflectivity = vec3(1);
    pgParams.diffuse = false;
}