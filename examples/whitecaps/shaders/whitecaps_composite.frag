#version 460

#extension GL_GOOGLE_include_directive : enable

#include "whitecaps_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 sky = textureLod(skySampler, uv, 0.0).rgb;
    float foam = textureLod(whitecapSampler, uv, 0.0).r;
    fragColor = vec4(mix(sky, vec3(1.0), foam), 1.0);
}
