#ifndef GLOBAL_CB_HLSL
#define GLOBAL_CB_HLSL

#include "../types.glsl"

#extension GL_EXT_scalar_block_layout : enable

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffu
#endif // UINT32_MAX

#ifndef GLOBAL_CB_SET
#define GLOBAL_CB_SET 0
#endif // GLOBAL_CB_SET

#ifdef GLOBAL_CB_SET
layout(set = GLOBAL_CB_SET, binding = 0, scalar) readonly buffer GlobalCB {
    REAL4X4_DP _ViewProjectionMatrix;
    REAL4X4_DP _InvViewProjectionMatrix;
    REAL3_DP _CameraPosition;
    REAL3_DP _SunDirection;
    REAL3_DP _WireFrameColor;
    REAL2_DP _ScreenSize;
    REAL_DP _Time;
    REAL_DP _CullFlag;
    REAL_DP _FoV;
    REAL_DP _WireFrameSize;
    REAL_DP _ScreenSpaceShadow;
    REAL_DP _FarPlaneDistance;
    uint _FrameIndex;
};


bool pre_rendering_frame() {
    return _FrameIndex == UINT32_MAX;
}
#endif // GLOBAL_CB_SET

#endif // GLOBAL_CB_HLSL
