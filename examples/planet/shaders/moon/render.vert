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

layout(set = 1, binding = 2, scalar) readonly buffer CurrentVertexBuffer {
    vec3 _CurrentVertexBuffer[];
};

layout(set = 1, binding = 3, scalar) readonly buffer CurrentDisplacementBuffer {
    vec3 _CurrentDisplacementBuffer[];
};

layout(set = 1, binding = 4, scalar) readonly buffer BisectorIndicesBuffer {
    uint _IndexedBisectorBuffer[];
};

layout(location = 0) out struct {
    vec3 positionRWS;
    vec3 positionORWS;
} vs;

void main() {
    uint triangleID = gl_VertexIndex / 3u;
    uint localVertexID = gl_VertexIndex % 3u;

    triangleID = _IndexedBisectorBuffer[triangleID];
    localVertexID = localVertexID == 0u ? 2u : (localVertexID == 2u ? 0u : 1u);

    uint vertexID = triangleID * 3u + localVertexID;
    REAL3_DP positionRWS = REAL3_DP(_CurrentVertexBuffer[vertexID]) + _UpdateCameraPosition - _CameraPosition;

    vs.positionORWS = vec3(positionRWS - REAL3_DP(_CurrentDisplacementBuffer[vertexID]));
    vs.positionRWS = vec3(positionRWS);

    gl_Position = vec4(_ViewProjectionMatrix * REAL4_DP(positionRWS, REAL_DP(1.0)));
}
