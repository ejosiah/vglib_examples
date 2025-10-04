#version 460

#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#include "shading.glsl"

layout(location = 0) in struct {
    vec4 color;
    vec3 worldPos;
    vec2 uv;
} fs_in;

layout(location = 3) noperspective in vec3 distance;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 extras;


void main() {
    vec3 N = sampleNormal(fs_in.worldPos.xz);

    vec3 color = mixWireFrame(vec3(0), distance);
    fragColor = vec4(color, 1);
    extras.x = gl_FragCoord.z;
}
