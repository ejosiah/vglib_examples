#ifndef MESH_COSNTANTS_GLSL
#define MESH_COSNTANTS_GLSL


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

#ifndef MESH_SKIP_INDIRECT_DISPATCH_BUFFER
layout(set = MESH_SET, binding = 5, scalar) buffer IndirectDispatchBuffer {
    uint _IndirectDispatchBuffer[];
};
#endif // MESH_SKIP_INDIRECT_DISPATCH_BUFFER

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

#define WORK_LIST_CLASSIFICATION 0
#define WORK_LIST_SIMPLIFICATION 1
#define WORK_LIST_ALLOCATE 2
#define WORK_LIST_PROPAGATE 3

layout(set = MESH_SET, binding = 11, scalar) buffer WorkListBuffers {
    uint buf[];
} _WorkListBuffers[];

#define _WorkListBuffer(index) _WorkListBuffers[index].buf
#define _ClassificationBuffer _WorkListBuffer(WORK_LIST_CLASSIFICATION)
#define _SimplificationBuffer _WorkListBuffer(WORK_LIST_SIMPLIFICATION)
#define _AllocateBuffer _WorkListBuffer(WORK_LIST_ALLOCATE)
#define _PropagateBuffer _WorkListBuffer(WORK_LIST_PROPAGATE)

#endif // MESH_SET



#endif // MESH_COSNTANTS_GLSL
