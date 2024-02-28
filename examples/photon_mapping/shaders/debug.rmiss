#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"
#include "structs.glsl"

layout(location = 0) rayPayloadIn HitRecord record;

void main(){
    record.hit = false;
}