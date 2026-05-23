#include "cbt/large//operators.h"

uint32_t find_msb(uint32_t x)
{
    uint32_t depth = 0;
    while (x > 0u) {
        ++depth;
        x >>= 1u;
    }
    return depth;
}

uint32_t find_msb_64(uint64_t x)
{
    uint32_t depth = 0;
    while (x > 0u) {
        ++depth;
        x >>= 1uLL;
    }
    return depth;
}

int32_t round_up_power2(uint32_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

uint32_t countbits(uint32_t i)
{
    i = i - ((i >> 1) & 0x55555555);        // add pairs of bits
    i = (i & 0x33333333) + ((i >> 2) & 0x33333333);  // quads
    i = (i + (i >> 4)) & 0x0F0F0F0F;        // groups of 8
    return (i * 0x01010101) >> 24;          // horizontal sum of bytes
}

uint32_t countbits(uint64_t i)
{
    return countbits(uint32_t(i & 0xffffffff)) + countbits(uint32_t((i >> 32) & 0xffffffff));
}