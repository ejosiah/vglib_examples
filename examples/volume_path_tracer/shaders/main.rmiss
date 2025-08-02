#version 460

#include "domain.glsl"

layout(location = 0) rayPayloadIn HitRecord hRec;

void main() {
    hRec.hit = false;
}