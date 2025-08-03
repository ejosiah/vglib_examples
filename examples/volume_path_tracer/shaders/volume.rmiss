#version 460

#include "domain.glsl"

layout(location = 1) rayPayloadIn float hitDistance;

void main() {
    hitDistance = 0;
}