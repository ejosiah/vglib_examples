#ifndef OCBT_GENERIC_H
#define OCBT_GENERIC_H

// Define the remaining values
#define BUFFER_ELEMENT_PER_LANE ((OCBT_TREE_NUM_SLOTS + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE)
#define BUFFER_ELEMENT_PER_LANE_NO_BITFIELD ((OCBT_TREE_NUM_SLOTS + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE)
#define BITFIELD_ELEMENT_PER_LANE ((OCBT_BITFIELD_NUM_SLOTS + WORKGROUP_SIZE - 1u) / WORKGROUP_SIZE)
#define WAVE_TREE_DEPTH uint(log2(OCBT_NUM_ELEMENTS))

const uint64_t one64_t = 1ul;
int bitCount64(uint64_t v) { return bitCount(uint(v)) + bitCount(uint(v >> 32u)); }

uint cbt_size()
{
    return OCBT_NUM_ELEMENTS;
}

uint last_level_offset()
{
    return OCBT_depth_offset[TREE_LAST_LEVEL] / 32u;
}

#if defined(CAN_USE_SHARED_MEMORY)
shared uint gs_cbtTree[OCBT_TREE_NUM_SLOTS];

// Function that sets a given bit
void set_bit(uint bitID, bool state)
{
    // Coordinates of the bit
    uint slot = bitID / 64u;
    uint local_id = bitID % 64u;

    if (state)
        _BitfieldBufferRW[slot] |= one64_t << local_id;
    else
        _BitfieldBufferRW[slot] &= ~(one64_t << local_id);
}

#if defined(CBT_ENABLE_ATOMIC_INT64)
void set_bit_atomic(uint bitID, bool state)
{
    // Coordinates of the bit
    uint slot = bitID / 64u;
    uint local_id = bitID % 64u;

    if (state)
        atomicOr(_BitfieldBufferRW[slot], one64_t << local_id);
    else
        atomicAnd(_BitfieldBufferRW[slot], ~(one64_t << local_id));
}
#endif // CBT_ENABLE_ATOMIC_INT64

uint get_bit(uint bitID)
{
    uint slot = bitID / 64u;
    uint local_id = bitID % 64u;
    return uint((_BitfieldBufferRW[slot] & (one64_t << local_id)) >> local_id);
}

uint get_heap_element(uint id)
{
    // Figure out the location of the first bit of this element
    uint real_heap_id = id - 1u;
    uint depth = uint(log2(real_heap_id + 1u));
    uint level_first_element = (1u << depth) - 1u;
    uint id_in_level = real_heap_id - level_first_element;
    uint first_bit = OCBT_depth_offset[depth] + OCBT_bit_count[depth] * id_in_level;
    if (depth < FIRST_VIRTUAL_LEVEL)
    {
        uint slot = first_bit / 32u;
        uint local_id = first_bit % 32u;
        uint target_bits = (gs_cbtTree[slot] >> local_id) & uint(OCBT_bit_mask[depth]);
        return (gs_cbtTree[slot] >> local_id) & uint(OCBT_bit_mask[depth]);
    }
    else
    {
        uint slot = first_bit / 64u;
        uint local_id = first_bit % 64u;
        uint64_t target_bits = (_BitfieldBufferRW[slot] >> local_id) & OCBT_bit_mask[depth];
        return uint(bitCount64(target_bits));
    }
}

// Should not be called if depth > TREE_LAST_LEVEL
void set_heap_element(uint id, uint value)
{
    // Figure out the location of the first bit of this element
    uint real_heap_id = id - 1u;
    uint depth = uint(log2(real_heap_id + 1u));
    uint level_first_element = (1u << depth) - 1u;
    uint first_bit = OCBT_depth_offset[depth] + OCBT_bit_count[depth] * (real_heap_id - level_first_element);

    // Find the slot and the local first bit
    uint slot = first_bit / 32u;
    uint local_id = first_bit % 32u;

    // Extract the relevant bits
    gs_cbtTree[slot] &= ~(uint(OCBT_bit_mask[depth]) << local_id);
    gs_cbtTree[slot] |= ((uint(OCBT_bit_mask[depth]) & value) << local_id);
}

// Should not be called if depth > TREE_LAST_LEVEL
void set_heap_element_atomic(uint id, uint value)
{
    // Figure out the location of the first bit of this element
    uint real_heap_id = id - 1u;
    uint depth = uint(log2(real_heap_id + 1u));
    uint level_first_element = (1u << depth) - 1u;
    uint first_bit = OCBT_depth_offset[depth] + OCBT_bit_count[depth] * (real_heap_id - level_first_element);

    // Find the slot and the local first bit
    uint slot = first_bit / 32u;
    uint local_id = first_bit % 32u;

    // Extract the relevant bits
    atomicAnd(gs_cbtTree[slot], ~(uint(OCBT_bit_mask[depth]) << local_id));
    atomicOr(gs_cbtTree[slot], ((uint(OCBT_bit_mask[depth]) & value) << local_id));
}

// Function that returns the number of active bits
uint bit_count()
{
    return gs_cbtTree[0u];
}

uint bit_count(uint depth, uint element)
{
    return get_heap_element((1u << depth) + element);
}

// decodes the position of the i-th one in the bitfield
uint decode_bit(uint handle)
{
#if defined(NAIVE_DECODE)
    uint bitID = 1u;
    for (uint currentDepth = 0u; currentDepth < WAVE_TREE_DEPTH; ++currentDepth)
    {
        uint heapValue = get_heap_element(2u * bitID);
        uint b = handle < heapValue ? 0u : 1u;

        bitID = 2u * bitID + b;
        handle -= heapValue * b;
    }

    return (bitID ^ OCBT_NUM_ELEMENTS);
#else
    uint currentDepth = 0u;
    uint heapElementID = 1u;
    for (currentDepth = 0u; currentDepth < FIRST_VIRTUAL_LEVEL; ++currentDepth)
    {
        // Read the left element
        uint heapValue = get_heap_element(2u * heapElementID);

        // Does it fall in the right or left subtree?
        uint b = handle < heapValue ? 0u : 1u;

        // Pick a subtree
        heapElementID = 2u * heapElementID + b;

        // Move the iterator to exclude the right subtree if required
        handle -= heapValue * b;
    }

    // Align with the internal depth
    currentDepth++;

    // Ok we have our subtree, now we need to pick the right bit
    uint64_t heapValue = _BitfieldBufferRW[heapElementID - OCBT_LAST_LEVEL_SIZE * 2u];
    uint64_t mask = 0xfffffffful;
    uint bitWidth = 32u;
    for (; currentDepth < (WAVE_TREE_DEPTH + 1u); ++currentDepth)
    {
        // Figure out the location of the first bit of this element
        uint real_heap_id =  2u * heapElementID - 1u;
        uint level_first_element = (1u << currentDepth) - 1u;
        uint id_in_level = real_heap_id - level_first_element;
        uint first_bit = bitWidth * id_in_level;
        uint local_id = first_bit % 64u;
        uint64_t target_bits = (heapValue >> local_id) & mask;
        uint heapValue = uint(bitCount64(target_bits));

        // Does it fall in the right or left subtree?
        uint b = handle < heapValue ? 0u : 1u;

        // Pick a subtree
        heapElementID = 2u * heapElementID + b;

        // Move the iterator to exclude the right subtree if required
        handle -= heapValue * b;

        // Adjust the mask and bitCount64
        bitWidth /= 2u;
        mask = mask >> bitWidth;
    }
    return (heapElementID ^ OCBT_NUM_ELEMENTS);
#endif
}

// decodes the position of the i-th zero in the bitfield
uint decode_bit_complement(uint handle)
{
#if defined(NAIVE_DECODE)
    uint bitID = 1u;
    uint c = OCBT_NUM_ELEMENTS / 2u;

    while (bitID < OCBT_NUM_ELEMENTS) {
        uint heapValue = c - get_heap_element(2u * bitID);
        uint b = handle < heapValue ? 0u : 1u;

        bitID = 2u * bitID + b;
        handle -= heapValue * b;
        c /= 2u;
    }

    return (bitID ^ OCBT_NUM_ELEMENTS);
#else
    uint heapElementID = 1u;
    uint c = OCBT_NUM_ELEMENTS / 2u;
    uint currentDepth = 0u;

    for (currentDepth = 0u; currentDepth < FIRST_VIRTUAL_LEVEL; ++currentDepth)
    {
        uint heapValue = c - get_heap_element(2u * heapElementID);
        uint b = handle < heapValue ? 0u : 1u;

        heapElementID = 2u * heapElementID + b;
        handle -= heapValue * b;
        c /= 2u;
    }

    // Align with the internal depth
    currentDepth++;

    // Ok we have our subtree, now we need to pick the right bit
    uint64_t heapValue = _BitfieldBufferRW[heapElementID - OCBT_LAST_LEVEL_SIZE * 2u];
    uint64_t mask = 0xfffffffful;
    uint bitWidth = 32u;
    for (; currentDepth < (WAVE_TREE_DEPTH + 1u); ++currentDepth)
    {
        // Figure out the location of the first bit of this element
        uint real_heap_id = 2u * heapElementID - 1u;
        uint level_first_element = (1u << currentDepth) - 1u;
        uint id_in_level = real_heap_id - level_first_element;
        uint first_bit = bitWidth * id_in_level;
        uint local_id = first_bit % 64u;
        uint64_t target_bits = (heapValue >> local_id) & mask;
        uint heapValue = c - uint(bitCount64(target_bits));

        uint b = handle < heapValue ? 0u : 1u;

        heapElementID = 2u * heapElementID + b;
        handle -= heapValue * b;
        c /= 2u;

        // Adjust the mask and bitCount
        bitWidth /= 2u;
        mask = mask >> bitWidth;
    }

    return (heapElementID ^ OCBT_NUM_ELEMENTS);
#endif
}

void reduce(uint groupIndex)
{
	// First we do a reduction until each lane has exactly one element to process
	uint initial_pass_size = OCBT_NUM_ELEMENTS / WORKGROUP_SIZE;
    for (uint it = initial_pass_size / 64u, offset = OCBT_NUM_ELEMENTS / 64u; it > 0u ; it >>= 1u, offset >>= 1u)
    {
        uint minHeapID = offset + (groupIndex * it);
        uint maxHeapID = offset + ((groupIndex + 1u) * it);

        for (uint heapID = minHeapID; heapID < maxHeapID; ++heapID)
        {
            set_heap_element(heapID, get_heap_element(heapID * 2u) + get_heap_element(heapID * 2u + 1u));
        }
    }
	barrier();

	for(uint s = WORKGROUP_SIZE / 2u; s > 0u; s >>= 1u)
    {
        if (groupIndex < s)
        {
            uint v = s + groupIndex;
            set_heap_element(v, get_heap_element(v * 2u) + get_heap_element(v * 2u + 1u));
        }
        barrier();
    }
}

void reduce_prepass(uint dispatchThreadID)
{
    // Initialize the packed sum
    uint packedSum = 0u;

    // Loop through the 4 pairs to process
    for (uint pairIdx = 0u; pairIdx < 4u; ++pairIdx)
    {
        // First element of the pair
        uint elementC = uint(bitCount64(_BitfieldBufferRW[dispatchThreadID * 8u + 2u * pairIdx]));

        // Second element of the pair
        elementC += uint(bitCount64(_BitfieldBufferRW[dispatchThreadID * 8u + 2u * pairIdx + 1u]));

        // Store in the right bits
        packedSum |= (elementC << pairIdx * 8u);
    }

    // Offset of the last level of the tree
    const uint bufferOffset = last_level_offset();

    // Store the result into the bitfield
    _CBTBufferRW[bufferOffset + dispatchThreadID] = packedSum;
}

void reduce_second_pass(uint groupIndex)
{
    // Load the lowest level (and only the last level)
    const uint level0Offset = OCBT_depth_offset[9u] / 32u;
    for (uint e = 0u; e < 4u; ++e)
    {
        uint target_element = 4u * groupIndex + e;
        gs_cbtTree[level0Offset + target_element] = _CBTBufferRW[level0Offset + target_element];
    }
    barrier();

    // First we do a reduction until each lane has exactly one element to process
    uint initial_pass_size = 256u;
    for (uint it = initial_pass_size / 64u, offset = initial_pass_size; it > 0u ; it >>= 1u, offset >>= 1u)
    {
        uint minHeapID = offset + (groupIndex * it);
        uint maxHeapID = offset + ((groupIndex + 1u) * it);

        for (uint heapID = minHeapID; heapID < maxHeapID; ++heapID)
        {
            set_heap_element(heapID, get_heap_element(heapID * 2u) + get_heap_element(heapID * 2u + 1u));
        }
    }
    barrier();
    
    for(uint s = WORKGROUP_SIZE / 2u; s > 0u; s >>= 1u)
    {
        if (groupIndex < s)
        {
            uint v = s + groupIndex;
            set_heap_element(v, get_heap_element(v * 2u) + get_heap_element(v * 2u + 1u));
        }
        barrier();
    }

    // Make sure all the previous operations are done
    barrier();

    // Load the bitfield to the LDS
    for (uint e = 0u; e < 5u; ++e)
    {
        uint target_element = 5u * groupIndex + e;
        if (target_element < 319u)
            _CBTBufferRW[target_element] = gs_cbtTree[target_element];
    }
}

void reduce_no_bitfield(uint groupIndex)
{
    // First we do a reduction until each lane has exactly one element to process
    uint initial_pass_size = OCBT_NUM_ELEMENTS / WORKGROUP_SIZE;
    for (uint it = initial_pass_size / 128u, offset = OCBT_NUM_ELEMENTS / 128u; it > 0u ; it >>= 1u, offset >>= 1u)
    {
        uint minHeapID = offset + (groupIndex * it);
        uint maxHeapID = offset + ((groupIndex + 1u) * it);

        for (uint heapID = minHeapID; heapID < maxHeapID; ++heapID)
        {
            set_heap_element(heapID, get_heap_element(heapID * 2u) + get_heap_element(heapID * 2u + 1u));
        }
    }
    barrier();

    for(uint s = WORKGROUP_SIZE / 2u; s > 0u; s >>= 1u)
    {
        if (groupIndex < s)
        {
            uint v = s + groupIndex;
            set_heap_element(v, get_heap_element(v * 2u) + get_heap_element(v * 2u + 1u));
        }
        barrier();
    }
}

void clear_cbt(uint groupIndex)
{
    for (uint v = 0u; v < BUFFER_ELEMENT_PER_LANE; ++v)
    {
        uint target_element = BUFFER_ELEMENT_PER_LANE * groupIndex + v;
        if (target_element < OCBT_TREE_NUM_SLOTS)
            gs_cbtTree[target_element] = 0u;
    }

    for (uint b = 0u; b < BITFIELD_ELEMENT_PER_LANE; ++b)
    {
        uint target_element = BITFIELD_ELEMENT_PER_LANE * groupIndex + b;
        if (target_element < OCBT_BITFIELD_NUM_SLOTS)
            _BitfieldBufferRW[target_element] = 0ul;
    }
    barrier();
}

// Importante note
// Depending on your target GPU architecture, the pattern used to load has a different performance behavior
// here is the best performant based on our tests:
// NVIDIA uint target_element = groupIndex + WORKGROUP_SIZE * e;
// AMD uint target_element = BUFFER_ELEMENT_PER_LANE * groupIndex + e;

void load_buffer_to_shared_memory(uint groupIndex)
{
    // Load the bitfield to the LDS
    for (uint e = 0u; e < BUFFER_ELEMENT_PER_LANE; ++e)
    {
        uint target_element = BUFFER_ELEMENT_PER_LANE * groupIndex + e;
        if (target_element < OCBT_TREE_NUM_SLOTS)
            gs_cbtTree[target_element] = _CBTBufferRW[target_element];
    }
    barrier();
}

void load_shared_memory_to_buffer(uint groupIndex)
{
    // Make sure all the previous operations are done
    barrier();

    // Load the bitfield to the LDS
    for (uint e = 0u; e < BUFFER_ELEMENT_PER_LANE; ++e)
    {
        uint target_element = BUFFER_ELEMENT_PER_LANE * groupIndex + e;
        if (target_element < OCBT_TREE_NUM_SLOTS)
            _CBTBufferRW[target_element] = gs_cbtTree[target_element];
    }
}
#endif // CAN_USE_SHARED_MEMORY

#if defined(CBT_ENABLE_ATOMIC_INT64)
void set_bit_atomic_buffer(uint bitID, bool state)
{
    // Coordinates of the bit
    uint slot = bitID / 64u;
    uint local_id = bitID % 64u;

    if (state)
        atomicOr(_BitfieldBufferRW[slot], one64_t << local_id);
    else
        atomicAnd(_BitfieldBufferRW[slot], ~(one64_t << local_id));
}
#endif // CBT_ENABLE_ATOMIC_INT64

#endif // OCBT_GENERIC_H
