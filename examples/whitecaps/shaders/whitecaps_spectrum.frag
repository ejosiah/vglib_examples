#version 460

#extension GL_GOOGLE_include_directive : enable

#include "whitecaps_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 p = mix(vec2(0.5), uv, pc.show_spectrum_zoom);
    vec4 spectrum = textureLod(spectrum_1_2_Sampler, p, 0.0);
    vec3 color = pc.show_spectrum_linear != 0 ? abs(spectrum.xyz) : log(vec3(1.0) + abs(spectrum.xyz) * 1000.0);
    fragColor = vec4(color, 1.0);
}
