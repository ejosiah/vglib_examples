#version 460 core

#include "lattice.glsl"

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 fracColor;

void main(){
    if(lattice(vUv) == 0) discard;

    fracColor = vec4(1, 0, 0, 1);
}