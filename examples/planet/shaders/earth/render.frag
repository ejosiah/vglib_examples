#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

layout(set = 1, binding = 1, scalar) readonly buffer PlanetCB {
    vec3 _PlanetCenter;
    float _PlanetRadius;
};

layout(location = 0) in struct {
    vec3 positionRWS;
    vec3 positionORWS;
} fs_in;

layout(location = 2) noperspective in vec3 dist;

layout(location = 0) out vec4 fragColor;

int _Attenuation = 1; // TODO add DeformationCB constants buffer

vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist);
vec3 evaluate_earth_lighting(REAL3_DP positionOPS, vec3 positionRWS, bool attenuation);

void main() {
    REAL3_DP positionOPS = REAL3_DP(fs_in.positionORWS) + _CameraPosition - _PlanetCenter;

    vec3 lighting = evaluate_earth_lighting(positionOPS, fs_in.positionRWS, bool(_Attenuation));

    lighting = apply_wireframe(lighting, _WireFrameColor, _WireFrameSize, dist);

    fragColor = vec4(1);
}

vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist) {
    if (wireframeSize > 0.0)
    {
        vec3 d2 = dist * dist;
        float nearest = min(min(d2.x, d2.y), d2.z);
        float f = exp2(-nearest / wireframeSize);
        color = mix(color, wireFrameColor, f);
    }
    return color;
}

vec3 evaluate_earth_lighting(REAL3_DP positionOPS, vec3 positionRWS, bool attenuation) {
    return vec3(1);
}
