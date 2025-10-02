#version 460

#include "tone_mapping.glsl"

layout(set = 0, binding = 0, input_attachment_index=0) uniform subpassInput colorInput;

layout(push_constant) uniform Constants {
    int method;
    float exposureValue;
};

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 color = subpassLoad(colorInput).rgb;
    color *= exp2(exposureValue);
    color = tone_map(color, method);
    color = pow(color, vec3(0.454));
    fragColor = vec4(color, 1);
}