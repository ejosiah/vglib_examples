#version 460

#include "../types.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

layout(set = 1, binding = 0, scalar) readonly buffer UpdateCB{
    REAL4X4_DP _UpdateViewProjectionMatrix;
    REAL4X4_DP _UpdateInvViewProjectionMatrix;
    REAL4_DP _FrustumPlanes[6];
    REAL3_DP _UpdateCameraPosition;
    REAL3_DP _UpdateCameraForward;
    REAL_DP _TriangleSize;
    REAL_DP _UpdateFOV;
    REAL_DP _UpdateFarPlaneDistance;
    uint _MaxSubdivisionDepth;
};


layout(set = 1, binding = 1, scalar) readonly buffer PlanetCB {
    REAL3_DP _PlanetCenter;
    REAL_DP _PlanetRadius;
};

layout(location = 0) in struct {
    vec3 positionRWS;
    vec3 positionORWS;
    vec3 normal;
} fs_in;

layout(location = 3) noperspective in vec3 dist;

layout(location = 0) out vec4 fragColor;

int _Attenuation = 1; // TODO add DeformationCB constants buffer

vec3 apply_wireframe(vec3 color, vec3 wireFrameColor, float wireframeSize, vec3 dist);
vec3 evaluate_earth_lighting(REAL3_DP positionOPS, vec3 positionRWS, bool attenuation);

void main() {
//    REAL3_DP positionOPS = REAL3_DP(fs_in.positionORWS) + _CameraPosition - _PlanetCenter;
//
//    vec3 lighting = evaluate_earth_lighting(positionOPS, fs_in.positionRWS, bool(_Attenuation));
    vec3 lighting = vec3(0, 0, 1);

    lighting = apply_wireframe(lighting, vec3(_WireFrameColor), float(_WireFrameSize), dist);

    vec3 N = normalize(fs_in.normal);
    vec3 V = normalize(vec3(_UpdateCameraForward));
    float ndov = dot(N, V);

    vec3 color = gl_FrontFacing ? vec3(0, 0, 1) : vec3(1, 0, 0);
    fragColor = vec4(lighting, 1);
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
