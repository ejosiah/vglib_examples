#ifndef CBT_CONSTANTS_GLSL
#define CBT_CONSTANTS_GLSL

#ifndef CBT_SET
#define CBT_SET 0
#endif // CBT_SET

#define CBT_TYPE_OCBT_128K (0)
#define CBT_TYPE_OCBT_256K (1)
#define CBT_TYPE_OCBT_512K (2)
#define CBT_TYPE_OCBT_1M (3)

#ifndef CBT_TYPE
#define CBT_TYPE CBT_TYPE_OCBT_128K
#endif // CBT_TYPE

layout(constant_id = 0) const int CBTType = CBT_TYPE;

#ifdef CBT_SET
layout(set = CBT_SET, binding = 0, scalar) buffer CBTBuffer {
    uint _CBTBufferRW[];
};

layout(set = CBT_SET, binding = 1, scalar) buffer BitfieldBuffer {
    uint64_t _BitfieldBufferRW[];
};

uint bit_count_buffer() { return _CBTBufferRW[0]; }

#endif // CBT_SET

#if CBT_TYPE == CBT_TYPE_OCBT_128K
#include "ocbt_128k.glsl"
#elif CBT_TYPE == CBT_TYPE_OCBT_256K
#include "ocbt_256k.glsl"
#elif CBT_TYPE == CBT_TYPE_OCBT_512K
#include "ocbt_512k.glsl"
#elif CBT_TYPE == CBT_TYPE_OCBT_1M
#include "ocbt_1m.glsl"
#else
#error "Unsupported CBT_TYPE"
#endif

#endif // CBT_CONSTANTS_GLSL
