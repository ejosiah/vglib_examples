#ifndef PLANET_MESH_COMMON_GLSL
#define PLANET_MESH_COMMON_GLSL

#define WORKGROUP_SIZE 64
#define UNSUPPORTED_FIRST_BIT_HIGH

#include "../shared_lib/bisector.glsl"

struct BisectorGeometry {
    vec3 p[4];
};

#ifndef MESH_UPDATE_SET
#define MESH_UPDATE_SET 0
#endif // MESH_UPDATE_SET

#ifdef MESH_UPDATE_SET
layout(set = MESH_UPDATE_SET, binding = 0, scalar) buffer MemoryBuffer {
    int _MemoryBuffer[];
};

layout(set = MESH_UPDATE_SET, binding = 1, scalar) buffer ValidationBuffer {
    uint _ValidationBuffer[];
};

#ifdef MESH_UPDATE_INDIRECT_BUFFER
layout(set = MESH_UPDATE_SET, binding = 2, scalar) buffer IndirectDispatchBuffer {
    uint _IndirectDispatchBuffer[];
};
#endif // MESH_UPDATE_INDIRECT_BUFFER
#endif // MESH_UPDATE_SET


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
