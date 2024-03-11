// Random Number Generator sourced from Ray Tracing Gems II
// chapter 14 - The Reference Path Tracer
// section 14.3.4 - Random Number Generation

#ifndef RANDOM_GLSL
#define RANDOM_GLSL


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



uint initRNG(vec2 pixelCoords, vec2 resolution, uint frameNumber) {
    uint seed = uint(dot(pixelCoords, vec2(1, resolution.x))) ^ jenkinsHash(frameNumber);
    return jenkinsHash(seed);
}


// Return random float in <0; 1) range (Xorshift-based version)
float rand(inout uint rngState) {
    return uintToFloat(xorshift(rngState));
}

#endif // RANDOM_GLSL
