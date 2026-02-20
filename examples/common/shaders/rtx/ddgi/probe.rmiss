#version 460

#extension GL_EXT_ray_tracing : enable

#include "shared.glsl"

layout(location = 0) rayPayloadIn RayPayload payload;

void main() {
    payload.radiance = vec3(0);
    payload.distance = 1000.0f;
}