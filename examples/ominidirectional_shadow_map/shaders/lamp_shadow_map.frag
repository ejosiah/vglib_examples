#version 460

#include "lattice.glsl"

layout(location = 0) in struct {
    vec3 viewSpacePos;
    vec2 uv;
} fs_in;

layout(location = 2) in flat float farPlane;

void main() {
    if(lattice(fs_in.uv) == 0) discard;

    gl_FragDepth = length(fs_in.viewSpacePos)/farPlane;
}