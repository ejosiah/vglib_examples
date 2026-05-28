#ifndef MESH_UTILITIES_GLSL
#define MESH_UTILITIES_GLSL

#include "../types.glsl"
#include "leb.glsl"

struct Triangle
{
    LEB_DATA_TYPE3 p[3];
};

void EvaluateElementPosition(uint64_t heapID, uint vertexDataOffset, uint minDepth, out Triangle parentTri, out Triangle childTri)
{
    uint depth = HeapIDDepth(heapID);
    uint64_t subTreeDepth = uint64_t(depth - minDepth);

    uint64_t baseHeapID = 1ul << (minDepth - 1u);
    uint primitiveID = uint((heapID >> subTreeDepth) - baseHeapID);

    LEB_DATA_TYPE3 p0 = LEB_DATA_TYPE3(_BaseVertexBuffer[3u * primitiveID + vertexDataOffset]);
    LEB_DATA_TYPE3 p1 = LEB_DATA_TYPE3(_BaseVertexBuffer[3u * primitiveID + 1u + vertexDataOffset]);
    LEB_DATA_TYPE3 p2 = LEB_DATA_TYPE3(_BaseVertexBuffer[3u * primitiveID + 2u + vertexDataOffset]);

    uint64_t mask = subTreeDepth != 0ul ? 0xfffffffffffffffful >> (64ul - subTreeDepth) : 0ul;
    uint64_t baseHeap = 1ul << subTreeDepth;
    uint64_t baseMask = mask & heapID;
    uint64_t subHeapID = baseMask + baseHeap;

    LEB_DATA_TYPE3 childArray[3] = LEB_DATA_TYPE3[3](
        LEB_DATA_TYPE3(p0.x, p1.x, p2.x),
        LEB_DATA_TYPE3(p0.y, p1.y, p2.y),
        LEB_DATA_TYPE3(p0.z, p1.z, p2.z));
    LEB_DATA_TYPE3 parentArray[3];

    leb_DecodeNodeAttributeArray_parent_child(subHeapID, childArray, parentArray);

    parentTri.p[0] = LEB_DATA_TYPE3(parentArray[0][0], parentArray[1][0], parentArray[2][0]);
    parentTri.p[1] = LEB_DATA_TYPE3(parentArray[0][1], parentArray[1][1], parentArray[2][1]);
    parentTri.p[2] = LEB_DATA_TYPE3(parentArray[0][2], parentArray[1][2], parentArray[2][2]);

    childTri.p[0] = LEB_DATA_TYPE3(childArray[0][0], childArray[1][0], childArray[2][0]);
    childTri.p[1] = LEB_DATA_TYPE3(childArray[0][1], childArray[1][1], childArray[2][1]);
    childTri.p[2] = LEB_DATA_TYPE3(childArray[0][2], childArray[1][2], childArray[2][2]);
}

#endif // MESH_UTILITIES_GLSL
