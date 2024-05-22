#version 460

#include "lighting.glsl"

layout(location = 0) in struct {
    vec4 color;
    vec3 worldPos;
    vec3 viewPos;
    vec3 normal;
    vec3 tanget;
    vec3 bitangent;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 normal = fs_in.normal;
    if(!gl_FrontFacing) {
        normal *= -1;
    }
    vec4 radiance = computeRadiance(createLightParams(fs_in.worldPos, fs_in.viewPos, normal, defaultLightDir, 2, fs_in.color.rgb));
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance.rgb / whitePoint * exposure), vec3(1.0 / 2.2));
}