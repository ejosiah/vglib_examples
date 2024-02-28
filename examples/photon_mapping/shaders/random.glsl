// Random Number Generator sourced from Ray Tracing Gems II
// chapter 14 - The Reference Path Tracer
// section 14.3.4 - Random Number Generation

#ifndef RANDOM_GLSL
#define RANDOM_GLSL

#ifndef USE_PCG
#define USE_PCG 0
#endif

#if USE_PCG
#define RngStateType uvec4
#else
#define RngStateType uint
#endif

uvec4 pcg4d(uvec4 v)
{
    v = v * 1664525u + 1013904223u;

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    v = v ^ (v >> 16u);

    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;

    return v;
}

uint xorshift(inout uint rngState)
{
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}

// Jenkins's "one at a time" hash function
uint jenkinsHash(uint x) {
    x += x << 10;
    x ^= x >> 6;
    x += x << 3;
    x ^= x >> 11;
    x += x << 15;
    return x;
}

// Converts unsigned integer into float int range <0; 1) by using 23 most significant bits for mantissa
float uintToFloat(uint x) {
    return uintBitsToFloat(0x3f800000u | (x >> 9)) - 1.0f;
}

    #if USE_PCG

RngStateType initRNG(vec2 pixelCoords, vec2 resolution, uint frameNumber) {
    return RngStateType(uvec2(pixelCoords), frameNumber, 0); //< Seed for PCG uses a sequential sample number in 4th channel, which increments on every RNG call and starts from 0
}

float rand(inout RngStateType rngState) {
    rngState.w++; //< Increment sample index
    return uintToFloat(pcg4d(rngState).x);
}

# else

RngStateType initRNG(vec2 pixelCoords, vec2 resolution, uint frameNumber) {
    RngStateType seed = uint(dot(pixelCoords, vec2(1, resolution.x))) ^ jenkinsHash(frameNumber);
    return jenkinsHash(seed);
}

RngStateType initRNG(uint initialSeed) {
    return jenkinsHash(initialSeed);
}

// Return random float in <0; 1) range (Xorshift-based version)
float rand(inout RngStateType rngState) {
    return uintToFloat(xorshift(rngState));
}

#endif

vec2 randomVec2(inout RngStateType rngState) {
    return vec2(rand(rngState), rand(rngState));
}

vec3 randomVec3(inout RngStateType rngState) {
    return vec3(rand(rngState), rand(rngState), rand(rngState));
}

vec4 randomVec4(inout RngStateType rngState) {
    return vec4(rand(rngState), rand(rngState), rand(rngState), rand(rngState));
}

#endif // RANDOM_GLSL
