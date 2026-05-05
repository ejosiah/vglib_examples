#version 460

#extension GL_GOOGLE_include_directive : enable

#include "whitecaps_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

float noise(vec2 p) {
    return texture(noiseSampler, p).r;
}

void main() {
    float n = 0.0;
    float amp = 0.5;
    vec2 p = uv;
    for (int i = 0; i < 5; ++i) {
        n += amp * noise(p);
        p *= 2.2;
        amp *= 0.55;
    }
    float cloud = smoothstep(0.45, 0.75, n);
    fragColor = vec4(pc.cloudColor.rgb, cloud * pc.cloudColor.a);
}
