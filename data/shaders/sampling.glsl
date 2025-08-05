#ifndef SAMPLING_GLSL
#define SAMPLING_GLSL

#include "constants.glsl"
#include "random.glsl"

#extension GL_EXT_debug_printf : require

float RadicalInverse_VdC(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;// / 0x100000000
}

vec2 hammersley(uint i, uint N){
    return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

vec3 uniformSampleSphere(vec3 u){
    return normalize(2 * u - 1);
}

vec3 uniformSampleSphere(vec2 u){
    float z = 1 - 2 * u.x;
    float r = sqrt(max(0, 1 - z * z));
    float phi = 2 * PI * u.y;
    return vec3(r * cos(phi), r * sin(phi), z);
}

vec3 sampleHemisphere(vec2 u, out float pdf){
    float a = sqrt(u.x);
    float b = TWO_PI * u.y;

    vec3 res = vec3(a * cos(b), a * sin(b), sqrt(1 - u.x));

    pdf = res.z * INV_PI;

    return res;
}

vec3 sampleHemisphere(vec2 u){
    float pdf;
    return sampleHemisphere(u, pdf);
}

vec3 cosineSampleHemisphere(vec2 u){
    // Uniformly sample disk.
    vec3 p;
    const float r   = sqrt(u.x);
    const float phi = 2.0f * PI * u.y;
    p.x             = r * cos(phi);
    p.y             = r * sin(phi);

    // Project up to hemisphere.
    p.z = sqrt(max(0.0f, 1.0f - p.x * p.x - p.y * p.y));

    return p;
}
vec2 uniformSampleTriangle(vec2 u){
    float sqx = sqrt(u.x);
    return vec2(1 - sqx, u.y * sqx);
}

float findInterval(sampler1D distribution, float value){
    int size = textureSize(distribution, 0);
    int first = 0;
    int len = size;
    while (len > 0){
        int half_len = len >> 1;
        int middle = first + half_len;
        float u = float(middle + .5)/float(size);
        float distValue = texture(distribution, u).r;
        if (distValue <= value){
            first =  middle + 1;
            len -= half_len + 1;
        } else {
            len = half_len;
        }
    }
    int index = clamp(first - 1, 0, size - 2);
    return float(index + .5)/size;
}

float findInterval(sampler2D distribution, float v, float value){
    int size = textureSize(distribution, 0).x;
    int first = 0;
    int len = size;
    while (len > 0){
        int half_len = len >> 1;
        int middle = first + half_len;
        float u = float(middle + .5)/float(size);
        float distValue = texture(distribution, vec2(u, v)).r;
        if (distValue <= value){
            first =  middle + 1;
            len -= half_len + 1;
        } else {
            len = half_len;
        }
    }
    int index = clamp(first - 1, 0, size - 2);
    return float(index + .5)/size;
}


float sampleContinuous1D(sampler1D func, sampler1D cdf, float funcIntegral, float u, out float pdf, out float off){
    float offset = findInterval(cdf, u);
    float offsetPOne = offset + 0.5/textureSize(cdf, 0);
    float cdfOffset = texture(cdf, offset).r;
    float cdfOffsetPOne = texture(cdf, offsetPOne).r;

    off = offset;
    float du = u - cdfOffset;

    if ((cdfOffsetPOne - cdfOffset)  > 0){
        du /= (cdfOffsetPOne - cdfOffset);
    }
    pdf = funcIntegral > 0 ? texture(func, offset).r / funcIntegral : 0;

    return (offset + du)/ float(textureSize(func, 0));
}

float sampleContinuous1D(sampler2D func, sampler2D cdf, float funcIntegral, float u, float v, out float pdf, out float off) {
    float offset = findInterval(cdf, v, u);
    float offsetPOne = offset + 0.5/textureSize(cdf, 0).x;
    float cdfOffset = texture(cdf, vec2(offset, v)).r;
    float cdfOffsetPOne = texture(cdf, vec2(offsetPOne, v)).r;

    off = offset;
    float du = u - cdfOffset;

    if ((cdfOffsetPOne - cdfOffset)  > 0){
        du /= (cdfOffsetPOne - cdfOffset);
    }
    pdf = funcIntegral > 0 ? texture(func, vec2(offset, v)).r / funcIntegral : 0;

    return (offset + du)/ float(textureSize(func, 0).x);
}

vec2 sampleContinuous2D(sampler2D pConditionalVFunc, sampler2D pConditionalVCdf, sampler1D pMarginal, sampler1D pMarginalCdf, float pMarginalIntegral, vec2 u, out float pdf){
    float pdf0;
    float pdf1;
    float v;
    float d1 = sampleContinuous1D(pMarginal, pMarginalCdf, pMarginalIntegral, u.y, pdf1, v);

    float off;
    float funcIntegral = texture(pMarginal, u.y).r;
    float d0 = sampleContinuous1D(pConditionalVFunc, pConditionalVCdf, funcIntegral, u.x, v, pdf0, off);

    pdf = pdf0 * pdf1;
    return vec2(d0, d1);
}

vec2 concentricSampleDisk(vec2 u){
    u = 2 * u - 1;

    if (u.x == 0  && u.y == 0) return vec2(0);// at origin

    float theta, r;
    if (abs(u.x) > abs(u.y)){
        r = u.x;
        theta = PI_OVER_FOUR * (u.y/u.x);
    } else {
        r = u.y;
        theta = PI_OVER_TWO - PI_OVER_FOUR * (u.x/u.y);
    }
    return r * vec2(cos(theta), sin(theta));
}

vec2 sampleHierarchicalMipMappedTexture(sampler2D distribution, vec2 u, out float pdf) {
    float u0 = u.x;
    float u1 = u.y;
    int maxMipLevel = textureQueryLevels(distribution) - 1;

    int x = 0, y = 0;
    for( int mip = maxMipLevel - 1; mip >= 0; --mip) {
        x <<= 1; y <<= 1;
        float left = texelFetch(distribution, ivec2(x, y), mip).r + texelFetch(distribution, ivec2(x, y+1), mip).r;
        float right = texelFetch(distribution, ivec2(x+1, y), mip).r + texelFetch(distribution, ivec2(x+1, y+1), mip).r;
        float probLeft = left /(left+right);

        if(u0 < probLeft) {
            u0 /= probLeft;
            float probLower = texelFetch(distribution, ivec2(x, y), mip).r / left;
            if(u1 < probLower) {
                u1 /= probLower;
            } else {
                y++;
                u1 = (u1 - probLower) / (1 - probLower);
            }
        } else {
            x++;
            u0 = (u0 - probLeft) / (1 - probLeft);
            float probLower = texelFetch(distribution, ivec2(x, y), mip).r / right;
            if( u1 < probLower) {
                u1 /= probLower;
            } else {
                y++;
                u1 = (u1 - probLower) / (1 - probLower);
            }
        }
    }

//    float num = texelFetch(distribution, ivec2(x, y), 0).r;
//    float denum = texelFetch(distribution, ivec2(0, 0), maxMipLevel).r;
//
//    debugPrintfEXT("levels = %d, [x, y] = [%d, %d] num = %f, denum = %f\n", maxMipLevel, x, y, num, denum);

    pdf = texelFetch(distribution, ivec2(x, y), 0).r / texelFetch(distribution, ivec2(0, 0), maxMipLevel).r;
    return vec2(x, y)/textureSize(distribution, 0);
}


#endif// SAMPLING_GLSL