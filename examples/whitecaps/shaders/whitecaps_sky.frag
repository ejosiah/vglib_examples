#version 460

#extension GL_GOOGLE_include_directive : enable

#include "whitecaps_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 horizon = vec3(0.42, 0.62, 0.82);
    vec3 zenith = vec3(0.02, 0.12, 0.36);
    vec3 color = mix(horizon, zenith, smoothstep(0.0, 1.0, uv.y));
    fragColor = vec4(color * pc.hdrExposure, 1.0);
}
