#ifndef WATER_CONSTANTS_GLSL
#define WATER_CONSTANTS_GLSL

#define WORKGROUP_RES 8
#define WATER_SIM_BAND_COUNT 4

#include "utils.glsl"

#ifndef WATER_DATA_SET
#define WATER_DATA_SET 0
#endif // WATER_DATA_SET

layout(set = WATER_DATA_SET, binding = 1) readonly buffer  WaterSimulationData {
    vec4 _PatchSize;
    vec4 _PatchWindOrientation;
    vec4 _PatchDirectionDampener;
    vec4 _PatchWindSpeed;
    uint _SimulationRes;
    float _SimulationTime;
    float _Choppiness;
    float _Amplification;
};

layout(set = WATER_DATA_SET, binding = 2, rg16f) uniform image2D SpectrumBuffer[WATER_SIM_BAND_COUNT];
layout(set = WATER_DATA_SET, binding = 3, rgba16f) uniform image2D DisplacementBuffer[WATER_SIM_BAND_COUNT];
layout(set = WATER_DATA_SET, binding = 4, rgba16f) uniform image2D SurfaceGradientBuffer[WATER_SIM_BAND_COUNT];


#ifndef WATER_SIM_SET
#define WATER_SIM_SET 1
#endif // WATER_SIM_SET

layout(set = WATER_SIM_SET, binding = 0, rgba16f) uniform image2D HImaginaryBuffer[WATER_SIM_BAND_COUNT];
layout(set = WATER_SIM_SET, binding = 1, rgba16f) uniform image2D FFTRowPassRealBuffer[WATER_SIM_BAND_COUNT];
layout(set = WATER_SIM_SET, binding = 2, rgba16f) uniform image2D FFTRowPassImaginaryBuffer[WATER_SIM_BAND_COUNT];

#endif // WATER_CONSTANTS_GLSL

