#version 460

#include "shared.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} fs_in;

layout(location = 3) flat in int isVisible;
layout(location = 4) noperspective in vec3 distance;

layout(location = 0) out vec4 fragColor;


void main() {
    #if 1
    vec4 wireColor = isVisible > 0 ? vec4(0.00,0.20,0.70, 0.5) : vec4(0.40,0.40,0.40,0.5);
    #else
    vec4 wireColor = vec4(0.0, 0.0, 0.0, 0.5);
    #endif
    fragColor = vec4(1);

    const float wireScale = 1.1; // scale of the wire in pixel
    vec3 distanceSquared = distance * distance;
    float nearestDistance = min(min(distanceSquared.x, distanceSquared.y), distanceSquared.z);
    float blendFactor = exp2(-nearestDistance / wireScale);

    vec3 normal = -1 + 2 * texture(u_NormalSampler, fs_in.uv).xzy;
    vec3 N = normalize(normal);

    vec3 L = normalize(vec3(1));
    vec3 albedo = fs_in.color;

    vec3 radiance = albedo * max(0, dot(N, L));
    radiance = pow(radiance, vec3(0.454));
    fragColor.rgb = mix(radiance, wireColor.xyz, blendFactor);
}