#version 460

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct BisectorData
{
// Subvision that should be applied to this bisector
    uint subdivisionPattern; // 0

// Allocated indices for this bisector
    uint indices[3]; // 1, 2, 3

// Neighbor that should be processed
    uint problematicNeighbor; // 4

// State of this bisector (split, merge, etc)
    uint bisectorState; // 5

// Visibility and modification flags of a bisector
    uint flags; // 6

// ID used for the propagation
    uint propagationID; //7
};


#define GLOBAL_CB_SET 0
#define GLOBAL_CB_BINDING_SLOT 0
#include "../shared_lib/constant_buffers.glsl"

#define MESH_SET 1
#define BISECTOR_GLSL
#include "../shared_lib/mesh_constants.glsl"

layout(set = 2, binding = 0, scalar) readonly buffer CBTBuffer {
    uint _CBTBufferRW[];
};

layout(set = 2, binding = 1, scalar) readonly buffer BitfieldBuffer {
    uint64_t _BitfieldBufferRW[];
};

layout(set = 3, binding = 0, scalar) readonly buffer LebMatrixCacheBuffer {
    mat3 _LebMatrixCache[];
};



layout(location = 0) out vec4 color;
layout(location = 1) out vec2 uv;

void main() {
    uint a = _CBTBufferRW[0];
    uint64_t b = _BitfieldBufferRW[0];
    mat3 c = _LebMatrixCache[0];
    uint start = _TotalNumElements - 4;
    uint e = _MaxSubdivisionDepth;

//    if(_FrameIndex == 0 && gl_VertexIndex == 0) {
//        for(uint i = start; i < _TotalNumElements; ++i) {
//            uvec3 pn = _NeighborsBuffer[0].v[i];
//        }
//
//
//        for(uint i = start; i < _TotalNumElements; ++i) {
//            uvec3 pn = _NeighborsBuffer[1].v[i];
//        }
//
//    }

    color = vec4(1);
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f - 1.0f, 0.5f, 1.0f);
}
