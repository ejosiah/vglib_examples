#version 460

#include "domain.glsl"

layout(location = 1) rayPayloadIn MediumHitRecord mRec;

void main() {
    mRec.t = 0;
}