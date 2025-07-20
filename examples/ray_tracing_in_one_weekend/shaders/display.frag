#version 460 core

#include "tone_mapping.glsl"

layout(binding = 0) uniform sampler2D image;

layout(push_constant) uniform Constants {
    int method;
    float exposureValue;
};

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main(){
    vec3 color = texture(image, vUv).rgb;
    color *= exp2(exposureValue);
    color = tone_map(color, method);
    color = pow(color, vec3(0.454));
    fragColor = vec4(color, 1);
}