#ifndef LEB_GLSL
#define LEB_GLSL

#ifdef LEB_MATRIX_SET
#define LEB_TABLE_DEPTH 5

layout(set = LEB_MATRIX_SET, binding = 0, scalar) readonly buffer LebMatrixCacheBuffer {
    mat3 _LebMatrixCache[];
};

shared mat3 g_MatrixCache[2 << LEB_TABLE_DEPTH];

void load_leb_matrix_cache_to_shared_memory(uint groupIndex)
{
    if (groupIndex < (2u << LEB_TABLE_DEPTH))
        g_MatrixCache[groupIndex] = _LebMatrixCache[groupIndex];
    barrier();
}
#endif

uint leb_depth(uint64_t heapID)
{
    return HeapIDDepth(heapID) - 1u;
}

uint64_t leb_GetBitValue(uint64_t bitField, int64_t bitID)
{
    return (bitField >> uint64_t(bitID)) & 1ul;
}

mat3 leb_SplittingMatrix(uint64_t bitValue)
{
    float b = float(bitValue);
    float c = 1.0 - b;
    return mat3(
        0.0, 0.5, b,
        b, 0.0, c,
        c, 0.5, 0.0);
}

dmat3 leb_SplittingMatrix64(uint64_t bitValue)
{
    double b = double(bitValue);
    double c = 1.0lf - b;
    return dmat3(
        0.0lf, 0.5lf, b,
        b, 0.0lf, c,
        c, 0.5lf, 0.0lf);
}

void leb_SplittingMatrix(inout mat3 mat, uint64_t bitValue)
{
    mat = leb_SplittingMatrix(bitValue) * mat;
}

void leb_SplittingMatrix(inout dmat3 mat, uint64_t bitValue)
{
    mat = leb_SplittingMatrix64(bitValue) * mat;
}

mat3 leb_SplittingMatrix_out(mat3 mat, uint64_t bitValue)
{
    return leb_SplittingMatrix(bitValue) * mat;
}

dmat3 leb_SplittingMatrix_out(dmat3 mat, uint64_t bitValue)
{
    return leb_SplittingMatrix64(bitValue) * mat;
}

void leb_DecodeTransformationMatrix_parent_child(uint64_t heapID, out mat3 parent, out mat3 child)
{
    int depth = int(leb_depth(heapID));
    parent = mat3(1.0);

    int bitID;
    for (bitID = depth - 1; bitID > 0; --bitID)
        leb_SplittingMatrix(parent, leb_GetBitValue(heapID, int64_t(bitID)));

    child = depth > 0 ? leb_SplittingMatrix_out(parent, leb_GetBitValue(heapID, int64_t(bitID))) : parent;
}

void leb_DecodeTransformationMatrix_parent_child_optimized(uint64_t heapID, out dmat3 parent, out dmat3 child)
{
    int depth = int(leb_depth(heapID));
    mat3 p0 = mat3(1.0);

    int firstStep = max(depth - 1 - 32, 0);
    int bitID;
    for (bitID = depth - 1; bitID > firstStep; --bitID)
        leb_SplittingMatrix(p0, leb_GetBitValue(heapID, int64_t(bitID)));

    mat3 p1 = mat3(1.0);
    for (bitID = firstStep; bitID > 0; --bitID)
        leb_SplittingMatrix(p1, leb_GetBitValue(heapID, int64_t(bitID)));

    parent = dmat3(p1) * dmat3(p0);
    child = depth > 0 ? leb_SplittingMatrix_out(parent, leb_GetBitValue(heapID, 0l)) : parent;
}

#ifdef LEB_MATRIX_SET
void leb_DecodeTransformationMatrix_parent_child_Tabulated(uint64_t heapID, out mat3 parent, out mat3 child)
{
    int depth = int(leb_depth(heapID));
    parent = mat3(1.0);

    const uint64_t msb = uint64_t(1u << LEB_TABLE_DEPTH);
    const uint64_t mask = uint64_t(~(~0u << LEB_TABLE_DEPTH));
    uint64_t parentHeapID = heapID / 2ul;
    while (parentHeapID > mask) {
        uint index = uint((parentHeapID & mask) | msb);
        parent = parent * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }
    if (parentHeapID != 0ul)
        parent = parent * g_MatrixCache[uint(parentHeapID)];

    child = depth > 0 ? leb_SplittingMatrix_out(parent, leb_GetBitValue(heapID, 0l)) : parent;
}

void leb_DecodeTransformationMatrix_parent_child_Tabulated(uint64_t heapID, out dmat3 parent, out dmat3 child)
{
    int depth = int(leb_depth(heapID));
    uint64_t parentHeapID = heapID / 2ul;
    const uint64_t msb = uint64_t(1u << LEB_TABLE_DEPTH);
    const uint64_t mask = uint64_t(~(~0u << LEB_TABLE_DEPTH));
    mat3 m1 = mat3(1.0);
    mat3 m2 = mat3(1.0);

    while ((parentHeapID > mask) && (parentHeapID > 0x00000000fffffffful)) {
        uint index = uint((parentHeapID & mask) | msb);
        m1 = m1 * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }

    while (parentHeapID > mask) {
        uint index = uint((parentHeapID & mask) | msb);
        m2 = m2 * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }
    if (parentHeapID != 0ul)
        m2 = m2 * g_MatrixCache[uint(parentHeapID)];

    parent = dmat3(m1) * dmat3(m2);
    child = depth > 0 ? leb_SplittingMatrix_out(parent, leb_GetBitValue(heapID, 0l)) : parent;
}
#endif

void leb_DecodeNodeAttributeArray_parent_child(uint64_t heapID, inout vec3 childAttribute[3], out vec3 parentAttribute[3])
{
    mat3 child;
    mat3 parent;
#ifdef LEB_MATRIX_SET
    leb_DecodeTransformationMatrix_parent_child_Tabulated(heapID, parent, child);
#else
    leb_DecodeTransformationMatrix_parent_child(heapID, parent, child);
#endif

    for (int i = 0; i < 3; ++i) {
        parentAttribute[i] = parent * childAttribute[i];
        childAttribute[i] = child * childAttribute[i];
    }
}

void leb_DecodeNodeAttributeArray_parent_child(uint64_t heapID, inout dvec3 childAttribute[3], out dvec3 parentAttribute[3])
{
    dmat3 child;
    dmat3 parent;
#ifdef LEB_MATRIX_SET
    leb_DecodeTransformationMatrix_parent_child_Tabulated(heapID, parent, child);
#else
    leb_DecodeTransformationMatrix_parent_child_optimized(heapID, parent, child);
#endif

    for (int i = 0; i < 3; ++i) {
        parentAttribute[i] = parent * childAttribute[i];
        childAttribute[i] = child * childAttribute[i];
    }
}

#endif // LEB_GLSL
