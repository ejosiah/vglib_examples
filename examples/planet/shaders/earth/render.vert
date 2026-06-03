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

    // Evaluate the properties of this triangle
    uint triangle_id = gl_VertexIndex / 3;
    uint local_vert_id = gl_VertexIndex % 3;

    // Operate the indirection
    triangle_id = _IndexedBisectorBuffer[triangle_id];


    // Which vertex should be read?
    local_vert_id = local_vert_id == 0 ? 2 : (local_vert_id == 2 ? 0 : 1);

//    if(_FrameIndex < 2) {
//    }

    // Camera relative world space position
    REAL3_DP positionRWS = REAL3_DP(_CurrentVertexBuffer[triangle_id * 3 + local_vert_id]) + _UpdateCameraPosition - _CameraPosition;
//    REAL3_DP positionRWS = REAL3_DP(_CurrentVertexBuffer[triangle_id * 3 + local_vert_id]) + _UpdateCameraPosition;

    // Original position
    vs.positionORWS = vec3(positionRWS - REAL3_DP(_CurrentDisplacementBuffer[triangle_id * 3 + local_vert_id]));
    vs.positionRWS = vec3(positionRWS);

    // Apply the view projection
    gl_Position = vec4(_ViewProjectionMatrix * REAL4_DP(positionRWS, REAL_DP(1.0)));
}
