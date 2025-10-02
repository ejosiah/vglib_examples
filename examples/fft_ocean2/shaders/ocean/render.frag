#version 460

#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#include "subdivision.glsl"

layout(location = 0) in struct {
    vec4 color;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 extras;

void main() {
    vec2 uv = getUV(fs_in.uv, 0);
    fragColor = vec4(uv, 0, 1);
    extras.x = gl_FragCoord.z;
}