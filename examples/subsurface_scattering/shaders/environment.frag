#version 460 core

#include "uniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D environment;
layout(set = 1, binding = 1) uniform sampler2D env_specular;
layout(set = 1, binding = 2) uniform sampler2D env_irradiance;

layout(location = 0) in vec3 texCord;
layout(location = 0) out vec4 fragColor;

void main(){
    vec3 dir = uniforms.envRotation * texCord;
    vec2 uv = 0.5 + 0.5 * octEncode(normalize(dir));
    vec3 color = texture(environment, uv).rgb;
    fragColor = vec4(pow(color, vec3(2.2)), 1);
}