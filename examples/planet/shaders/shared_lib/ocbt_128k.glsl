#ifndef OCBT_128K_GLSL
#define OCBT_128K_GLSL

// The maximal size of the LDS is 16kbyte.
#ifndef WORKGROUP_SIZE
#define WORKGROUP_SIZE 64
#endif

/*
Level 0: 32 bit // [0, 131072] x 1, needs a minimum of 18 bits (rounded up to 32 for alignment and required for atomic operations)
Level 1: 32 bit // [0, 65536] x 2, needs a minimum of 17 bits (rounded up to 32 for alignment and required for atomic operations)
Level 2: 32 bit // [0, 32768] x 4, needs a minimum of 16 bits (bumped to 32 bits for atomic operations)
Level 3: 32 bit // [0, 16384] x 8, needs a minimum of 15 bits (rounded up to 16 for alignment and bumped to 32 bits for atomic operations)
Level 4: 32 bit // [0, 8192] x 16, needs a minimum of 14 bits (rounded up to 16 for alignment and bumped to 32 bits for atomic operations)
Level 5: 32 bit // [0, 4096] x 32, needs a minimum of 13 bits (rounded up to 16 for alignment and bumped to 32 bits for atomic operations)
Level 6: 32 bit // [0, 2048] x 64, needs a minimum of 12 bits (rounded up to 16 for alignment and bumped to 32 bits for atomic operations)

Level 7: 16 bit // [0, 1024] x 128, needs a minimum of 11 bits (rounded up to 16 for alignment)
Level 8: 16 bit // [0, 512] x 256, needs a minimum of 10 bits (rounded up to 16 for alignment)
Level 9: 16 bit // [0, 256] x 512, needs a minimum of 9 bits (rounded up to 16 for alignment)
Level 10: 8 bit // [0, 128] x 1024, needs a minimum of 8 bits

Level 11: Raw 64 bits representation
*/

// Num elements
#define OCBT_NUM_ELEMENTS 131072u
// Tree sizes
#define OCBT_TREE_SIZE_BITS (32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u + 32u * 64u + 16u * 128u + 16u * 256u + 16u * 512u + 8u * 1024u)
#define OCBT_TREE_NUM_SLOTS (OCBT_TREE_SIZE_BITS / 32u)
#define OCBT_BITFIELD_NUM_SLOTS (OCBT_NUM_ELEMENTS / 64u)
#define OCBT_LAST_LEVEL_SIZE 1024u

// Tree last level
#define TREE_LAST_LEVEL 10u
// First virtual level
#define FIRST_VIRTUAL_LEVEL 11u
// Leaf level
#define LEAF_LEVEL 17u

// per level offset
const uint OCBT_depth_offset[18] = uint[18](
    0u, // Level 0
    32u * 1u, // level 1
    32u * 1u + 32u * 2u, // level 2
    32u * 1u + 32u * 2u + 32u * 4u, // level 3
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u, // Level 4
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u, // Level 5
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u, // Level 6
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u + 32u * 64u, // Level 7

    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u + 32u * 64u + 16u * 128u, // Level 8
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u + 32u * 64u + 16u * 128u + 16u * 256u, // Level 9
    32u * 1u + 32u * 2u + 32u * 4u + 32u * 8u + 32u * 16u + 32u * 32u + 32u * 64u + 16u * 128u + 16u * 256u + 16u * 512u, // Level 10

    0u, // Level 12
    0u, // Level 13
    0u, // Level 14
    0u, // Level 15
    0u, // Level 16
    0u, // Level 17
    0u // Level 18
);

const uint64_t OCBT_bit_mask[18] = uint64_t[18](
    0xfffffffful, // Root 17
    0xfffffffful, // Level 16
    0xfffffffful, // level 15
    0xfffffffful, // level 14
    0xfffffffful, // level 13
    0xfffffffful, // level 12
    0xfffffffful, // level 11

    0xfffful, // level 10
    0xfffful, // level 9
    0xfffful, // level 8
    0xfful, // level 8

    0xfffffffffffffffful, // level 7
    0xfffffffful, // Level 6
    0xfffful, // level 5
    0xfful, // level 4
    0xful, // level 3
    0x3ul, // level 2
    0x1ul // level 1
);

const uint OCBT_bit_count[18] = uint[18](
    32u, // Root 17
    32u, // Level 16
    32u, // level 15
    32u, // level 14
    32u, // level 13
    32u, // level 12
    32u, // level 11

    16u, // level 10
    16u, // level 9
    16u, // level 8
    8u, // level 8

    64u, // Level 5
    32u, // Level 5
    16u, // Level 4
    8u, // level 3
    4u, // level 2
    2u, // level 1
    1u // level 0
);

#include "ocbt_generic.glsl"

#if defined(CAN_USE_SHARED_MEMORY)
void reduce_first_pass(uint dispatchThreadID, uint groupIndex)
{
    // Load the lowest level (and only the last level)
    const uint level0Offset = OCBT_depth_offset[TREE_LAST_LEVEL] / 32u;
    if (groupIndex % 2u == 0u)
        gs_cbtTree[level0Offset + dispatchThreadID / 2u] = _CBTBufferRW[level0Offset + dispatchThreadID / 2u];

    barrier();

    // First we do a reduction until each lane has exactly one element to process
    uint initial_pass_size = OCBT_LAST_LEVEL_SIZE / 2u;
    uint it, offset;
    for (it = initial_pass_size / 512u, offset = initial_pass_size; it > 1u; it >>= 1, offset >>= 1)
    {
        uint minHeapID = offset + (dispatchThreadID * it);
        uint maxHeapID = offset + ((dispatchThreadID + 1u) * it);

        for (uint heapID = minHeapID; heapID < maxHeapID; ++heapID)
        {
            set_heap_element(heapID, get_heap_element(heapID * 2u) + get_heap_element(heapID * 2u + 1u));
        }
    }

    // Last pass needs to be atomic
    uint heapID = offset + (dispatchThreadID * it);
    set_heap_element_atomic(heapID, get_heap_element(heapID * 2u) + get_heap_element(heapID * 2u + 1u));

    barrier();

    const uint level2Offset = OCBT_depth_offset[TREE_LAST_LEVEL - 1u] / 32u;
    if (groupIndex % 2u == 0u)
        _CBTBufferRW[level2Offset + dispatchThreadID / 2u] = gs_cbtTree[level2Offset + dispatchThreadID / 2u];

    const uint level3Offset = OCBT_depth_offset[TREE_LAST_LEVEL - 2u] / 32u;
    if (groupIndex % 4u == 0u)
        _CBTBufferRW[level3Offset + dispatchThreadID / 4u] = gs_cbtTree[level3Offset + dispatchThreadID / 4u];
}
#endif // CAN_USE_SHARED_MEMORY

#endif // OCBT_128K_GLSL
