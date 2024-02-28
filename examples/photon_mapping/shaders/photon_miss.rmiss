#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "structs.glsl"

layout(location = 0) rayPayloadIn PhotonGenParams pgParams;

void main(){
    pgParams.diffuse = false;
    pgParams.reflectivity = vec3(0);
}