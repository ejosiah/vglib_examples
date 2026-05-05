#ifndef WHITECAPS_COMMON_GLSL
#define WHITECAPS_COMMON_GLSL

#define PI 3.14159265358979323846
#define TWO_PI 6.28318530717958647692
#define FFT_SIZE 256
#define PASSES 8
#define WAVE_LAYERS 8

layout(push_constant) uniform WhitecapsControls {
    vec4 GRID_SIZES;
    vec4 choppy_factor;
    vec4 seaColor;
    vec4 cloudColor;
    vec4 sunDirection;
    float WIND;
    float OMEGA;
    float amplitude;
    float t;
    float hdrExposure;
    float jacobian_scale;
    float gridSize;
    float show_spectrum_zoom;
    int show_spectrum_linear;
    int flags;
    int pass;
    int layerOffset;
} pc;

layout(set = 0, binding = 0, rgba32f) uniform image2D spectrum_1_2_Image;
layout(set = 0, binding = 1, rgba32f) uniform image2D spectrum_3_4_Image;
layout(set = 0, binding = 2, rgba32f) uniform image2DArray fftWavesImage;
layout(set = 0, binding = 3, rgba32f) uniform image2DArray fftTempImage;
layout(set = 0, binding = 4, rgba32f) uniform image2D butterflyImage;
layout(set = 0, binding = 5, rgba32f) uniform image3D slopeVarianceImage;
layout(set = 0, binding = 6, rgba32f) uniform image2D whitecapImage;
layout(set = 0, binding = 7, rgba32f) uniform image2D skyImage;

layout(set = 0, binding = 8) uniform sampler2D spectrum_1_2_Sampler;
layout(set = 0, binding = 9) uniform sampler2D spectrum_3_4_Sampler;
layout(set = 0, binding = 10) uniform sampler2DArray fftWavesSampler;
layout(set = 0, binding = 11) uniform sampler2D butterflySampler;
layout(set = 0, binding = 12) uniform sampler3D slopeVarianceSampler;
layout(set = 0, binding = 13) uniform sampler2D whitecapSampler;
layout(set = 0, binding = 14) uniform sampler2D skySampler;
layout(set = 0, binding = 15) uniform sampler2D noiseSampler;

float sqr(float x) {
    return x * x;
}

float omega(float k) {
    return sqrt(9.81 * k * (1.0 + sqr(k / 370.0)));
}

#endif
