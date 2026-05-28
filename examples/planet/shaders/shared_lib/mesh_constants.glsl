#ifndef MESH_COSNTANTS_GLSL
#define MESH_COSNTANTS_GLSL

#include "../types.glsl"

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_nonuniform_qualifier : require

#ifndef MESH_SET
#define MESH_SET 0
#endif // MESH_SET

#ifdef MESH_SET
layout(set = MESH_SET, binding = 0, scalar) readonly buffer GeometryUBO {
    uint _TotalNumElements;
    uint _BaseDepth;
    uint _TotalNumVertices;
    uint _MaterialID;
};

layout(set = MESH_SET, binding = 1, scalar) readonly buffer UpdateCB{
    mat4 _UpdateViewProjectionMatrix;
    mat4 _UpdateInvViewProjectionMatrix;
    vec4 _FrustumPlanes[6];
    vec3 _UpdateCameraPosition;
    vec3 _UpdateCameraForward;
    float _TriangleSize;
    uint _MaxSubdivisionDepth;
    float _UpdateFOV;
    float _UpdateFarPlaneDistance;
};

layout(set = MESH_SET, binding = 2, scalar) buffer NeighborsBuffer {
    uvec3 v[];
} _NeighborsBuffer[2];

layout(set = MESH_SET, binding = 3, scalar) buffer CurrentVertexBuffer {
    vec3 _CurrentVertexBuffer[];
};

layout(set = MESH_SET, binding = 4, scalar) buffer IndirectDrawBuffer {
    uint _IndirectDrawBuffer[];
};

layout(set = MESH_SET, binding = 5, scalar) buffer MeshIndirectDispatchBuffer {
    uint _IndirectDispatchBuffer[];
} mesh;

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

#ifdef BISECTOR_GLSL
layout(set = MESH_SET, binding = 10, scalar) buffer BisectorDataBuffer {
    BisectorData _BisectorDataBuffer[];
};
#endif

layout(set = MESH_SET, binding = 11, scalar) buffer WorkListBuffers {
    uint buf[];
} _WorkListBuffers[];

layout(set = MESH_SET, binding = 12, scalar) readonly buffer BaseVertexBuffer {
    vec3 _BaseVertexBuffer[];
};

layout(set = MESH_SET, binding = 13, scalar) buffer LEBPositionBuffer {
    REAL3_DP _LEBPositionBuffer[];
};

layout(set = MESH_SET, binding = 14, scalar) readonly buffer PlanetCB {
    vec3 _PlanetCenter;
    float _PlanetRadius;
};

#define WORK_LIST_CLASSIFICATION 0
#define WORK_LIST_SIMPLIFICATION 1
#define WORK_LIST_ALLOCATE 2
#define WORK_LIST_PROPAGATE 3

#define _WorkListBuffer(index) _WorkListBuffers[index].buf
#define _ClassificationBuffer _WorkListBuffer(WORK_LIST_CLASSIFICATION)
#define _SimplificationBuffer _WorkListBuffer(WORK_LIST_SIMPLIFICATION)
#define _AllocateBuffer _WorkListBuffer(WORK_LIST_ALLOCATE)
#define _PropagateBuffer _WorkListBuffer(WORK_LIST_PROPAGATE)

#endif // MESH_SET



#endif // MESH_COSNTANTS_GLSL
