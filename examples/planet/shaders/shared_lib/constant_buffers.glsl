#ifndef GLOBAL_CB_HLSL
#define GLOBAL_CB_HLSL

#extension GL_EXT_scalar_block_layout : enable

#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffu
#endif // UINT32_MAX

#ifndef GLOBAL_CB_SET
#define GLOBAL_CB_SET 0
#endif // GLOBAL_CB_SET

#ifdef GLOBAL_CB_SET
layout(set = GLOBAL_CB_SET, binding = 0, scalar) buffer GlobalCB {
    mat4 _ViewProjectionMatrix;
    mat4 _InvViewProjectionMatrix;
    vec3 _CameraPosition;
    vec3 _SunDirection;
    vec3 _WireFrameColor;
    vec2 _ScreenSize;
    uint _FrameIndex;
    float _Time;
    float _CullFlag;
    float _FoV;
    float _WireFrameSize;
    float _ScreenSpaceShadow;
    float _FarPlaneDistance;
};


bool pre_rendering_frame() {
    return _FrameIndex == UINT32_MAX;
}
#endif // GLOBAL_CB_SET

#endif // GLOBAL_CB_HLSL
