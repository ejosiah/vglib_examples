#version 460 core

#include "common.glsl"

layout(push_constant) uniform Constants {
    layout(offset=192)
    vec3 cell;
};

vec3 cell_type[8] = vec3[8](
    vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 1, 0),
    vec3(0, 1, 1), vec3(1, 0, 1), vec3(0.05, 0.4, 0.8), vec3(0.9, 0.45, 0.05)
);

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor = vec4(1);

    ivec3 c = intCoord(cell);
    uint index = CELL_TYPE_INDEX(c.x, c.y, c.z);
    fragColor.rgb = cell_type[index];
}