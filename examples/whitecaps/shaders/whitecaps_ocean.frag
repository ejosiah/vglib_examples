#version 460

#extension GL_GOOGLE_include_directive : enable

#include "whitecaps_common.glsl"

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

float waveHeight(vec2 p) {
    vec4 h = textureLod(fftWavesSampler, vec3(fract(p / pc.GRID_SIZES.x), 0.0), 0.0);
    return h.x + h.y + h.z + h.w;
}

vec2 displacement(vec2 p) {
    vec4 d12 = textureLod(fftWavesSampler, vec3(fract(p / pc.GRID_SIZES.x), 3.0), 0.0);
    vec4 d34 = textureLod(fftWavesSampler, vec3(fract(p / pc.GRID_SIZES.z), 4.0), 0.0);
    return pc.choppy_factor.x * d12.xy +
           pc.choppy_factor.y * d12.zw +
           pc.choppy_factor.z * d34.xy +
           pc.choppy_factor.w * d34.zw;
}

vec3 normalAt(vec2 p, float footprint) {
    float h = waveHeight(p);
    float hx = waveHeight(p + vec2(footprint, 0.0));
    float hy = waveHeight(p + vec2(0.0, footprint));
    return normalize(vec3(h - hx, footprint * 0.65, h - hy));
}

vec3 skyColor(vec2 q) {
    vec3 horizon = vec3(0.52, 0.70, 0.86);
    vec3 zenith = vec3(0.03, 0.16, 0.38);
    float t = smoothstep(0.48, 1.0, q.y);
    vec3 sky = mix(horizon, zenith, t);
    float sun = pow(max(dot(normalize(vec3(q.x - 0.72, q.y - 0.72, 0.35)), normalize(pc.sunDirection.xyz)), 0.0), 240.0);
    return sky + sun * vec3(1.0, 0.78, 0.42);
}

void main() {
    const float horizon = 0.57;
    vec3 sky = skyColor(uv);

    if (uv.y > horizon) {
        fragColor = vec4(sky * pc.hdrExposure, 1.0);
        return;
    }

    vec2 ndc = uv * 2.0 - 1.0;
    float depth = clamp((horizon - uv.y) / horizon, 0.002, 1.0);
    float distance = 70.0 / (depth * depth + 0.018);
    vec2 world = vec2(ndc.x * distance * 1.35, distance);
    world += displacement(world) * 5.0;

    float footprint = max(1.0, distance * 0.015);
    float height = waveHeight(world);
    float foam = textureLod(whitecapSampler, fract(world / pc.GRID_SIZES.x), 0.0).r;
    vec3 n = normalAt(world, footprint);
    vec3 l = normalize(vec3(0.2, 0.85, 0.38));
    vec3 v = normalize(vec3(-ndc.x * 0.25, 0.35 + depth, 1.0));
    vec3 h = normalize(l + v);
    float diffuse = clamp(dot(n, l), 0.0, 1.0);
    float specular = pow(max(dot(n, h), 0.0), 180.0);
    float fresnel = pow(1.0 - max(dot(n, v), 0.0), 5.0);
    vec3 reflection = skyColor(vec2(0.5 + n.x * 0.28, 0.58 + n.z * 0.22));
    vec3 sea = pc.seaColor.rgb * (0.28 + 0.72 * diffuse);
    vec3 color = mix(sea, reflection, 0.18 + 0.62 * fresnel) + specular * vec3(1.0, 0.86, 0.62);
    color += height * 0.015;
    color = mix(color, vec3(1.0), clamp(foam * (0.25 + 0.75 * depth), 0.0, 1.0));
    color = mix(sky, color, smoothstep(0.0, 0.08, horizon - uv.y));
    fragColor = vec4(color * pc.hdrExposure, 1.0);
}
