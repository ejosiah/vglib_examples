#ifndef PLANET_MESH_COMMON_GLSL
#define PLANET_MESH_COMMON_GLSL

#include "../shared_lib/bisector.glsl"

#ifndef MESH_UPDATE_SET
#define MESH_UPDATE_SET 0
#endif // MESH_UPDATE_SET
layout(set = MESH_UPDATE_SET, binding = 0, scalar) buffer MemoryBuffer {
    int _MemoryBuffer[];
};

layout(set = MESH_UPDATE_SET, binding = 1, scalar) buffer ValidationBuffer {
    uint _ValidationBuffer[];
};


#ifndef MESH_SET
#define MESH_SET 1
#endif // MESH_SET
layout(set = MESH_SET, binding = 2, scalar) buffer NeighborsBuffer {
    uvec3 v[];
} _NeighborsBuffer[2];

layout(set = MESH_SET, binding = 3, scalar) buffer CurrentVertexBuffer {
    vec3 _CurrentVertexBuffer[];
};

layout(set = MESH_SET, binding = 4, scalar) buffer IndirectDrawBuffer {
    uint _IndirectDrawBuffer[];
};

layout(set = MESH_SET, binding = 5, scalar) buffer IndirectDispatchBuffer {
    uint _IndirectDispatchBuffer[];
};

layout(set = MESH_SET, binding = 6, scalar) buffer BisectorIndicesBuffer {
    uint _BisectorIndicesBuffer[];
};

layout(set = MESH_SET, binding = 7, scalar) buffer VisibleBisectorIndicesBuffer {
    uint _VisibleBisectorIndicesBuffer[];
};

layout(set = MESH_SET, binding = 8, scalar) buffer ModifiedBisectorIndicesBuffer {
    uint _ModifiedBisectorIndicesBuffer[];
};

// Bisector buffers
layout(set = MESH_SET, binding = 9, scalar) buffer HeapIDBuffer {
    uint64_t _HeapIDBuffer[];
};

layout(set = MESH_SET, binding = 10, scalar) buffer BisectorDataBuffer {
    BisectorData _BisectorDataBuffer[];
};

layout(set = MESH_SET, binding = 11, scalar) buffer ClassificationBuffer {
    uint _ClassificationBuffer[];
};

layout(set = MESH_SET, binding = 12, scalar) buffer SimplificationBuffer {
    uint _SimplificationBuffer[];
};

layout(set = MESH_SET, binding = 13, scalar) buffer AllocateBuffer {
    int _AllocateBuffer[];
};

layout(set = MESH_SET, binding = 14, scalar) buffer PropagateBuffer {
    int _PropagateBuffer[];
};


// Debug buffers


// Possible splits
#define NO_SPLIT 0x00
#define CENTER_SPLIT 0x01
#define RIGHT_SPLIT 0x02
#define LEFT_SPLIT 0x04
#define RIGHT_DOUBLE_SPLIT (CENTER_SPLIT | RIGHT_SPLIT)
#define LEFT_DOUBLE_SPLIT (CENTER_SPLIT | LEFT_SPLIT)
#define TRIPLE_SPLIT (CENTER_SPLIT | RIGHT_SPLIT | LEFT_SPLIT)

// Split buffer slots
#define SPLIT_COUNTER 0
#define SIMPLIFY_COUNTER 1
#define CLASSIFY_COUNTER_OFFSET 2

#endif // PLANET_MESH_COMMON_GLSL
