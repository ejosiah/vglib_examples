#version 460
#extension GL_EXT_ray_tracing : enable

#include "structs.glsl"

layout(location = 0) rayPayloadInEXT RtParams rtParams;

void main(){
    rtParams.objectType = OBJECT_TYPE_NONE;
    rtParams.color = vec3(0.0, 0.0, 0.2);
}