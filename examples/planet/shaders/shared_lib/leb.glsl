#ifndef LEB_GLSL
#define LEB_GLSL

#ifdef LEB_MATRIX_SET
#define LEB_TABLE_DEPTH 5ul

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

#if defined(UNSUPPORTED_FIRST_BIT_HIGH)
uint leb_depth(uint64_t heapID)
{
    uint depth = 0u;
    while (heapID > 0ul)
    {
        ++depth;
        heapID >>= 1ul;
    }
    return depth - 1u;
}
#else
uint leb_depth(uint64_t heapID)
{
    return heapID != 0ul ? uint(findMSB(heapID)) : 0u;
}
#endif

uint64_t leb__GetBitValue(uint64_t bitField, int64_t bitID)
{
    return (bitField >> uint64_t(bitID)) & 1ul;
}

void leb__IdentityMatrix3x3(out mat3 m)
{
    m = mat3(1);
}

void leb__IdentityMatrix3x3(out dmat3 m)
{
    m = dmat3(1);
}

void leb__SplittingMatrix(inout mat3 mat, uint64_t bitValue)
{
    float b = float(bitValue);
    float c = 1.0 - b;
    mat3 splitMatrix = mat3(
        0.0, 0.5, b,
        b, 0.0, c,
        c, 0.5, 0.0);
    mat = splitMatrix * mat;
}

void leb__SplittingMatrix(inout dmat3 mat, uint64_t bitValue)
{
    double b = double(bitValue);
    double c = 1.0lf - b;
    dmat3 splitMatrix = dmat3(
        0.0lf, 0.5lf, b,
        b, 0.0lf, c,
        c, 0.5lf, 0.0lf);
    mat = splitMatrix * mat;
}

mat3 leb__SplittingMatrix_out(mat3 mat, uint64_t bitValue)
{
    float b = float(bitValue);
    float c = 1.0 - b;
    mat3 splitMatrix = mat3(
        0.0, 0.5, b,
        b, 0.0, c,
        c, 0.5, 0.0);
    return splitMatrix * mat;
}

dmat3 leb__SplittingMatrix_out(dmat3 mat, uint64_t bitValue)
{
    double b = double(bitValue);
    double c = 1.0lf - b;
    dmat3 splitMatrix = dmat3(
        0.0lf, 0.5lf, b,
        b, 0.0lf, c,
        c, 0.5lf, 0.0lf);
    return splitMatrix * mat;
}

void leb__DecodeTransformationMatrix(uint64_t heapID, out mat3 mat)
{
    int depth = int(leb_depth(heapID));
    leb__IdentityMatrix3x3(mat);
    for (int bitID = depth - 1; bitID >= 0; --bitID)
        leb__SplittingMatrix(mat, leb__GetBitValue(heapID, int64_t(bitID)));
}

void leb__DecodeTransformationMatrix(uint64_t heapID, out dmat3 mat)
{
    int depth = int(leb_depth(heapID));
    leb__IdentityMatrix3x3(mat);
    for (int bitID = depth - 1; bitID >= 0; --bitID)
        leb__SplittingMatrix(mat, leb__GetBitValue(heapID, int64_t(bitID)));
}

#ifdef LEB_MATRIX_SET
void leb__DecodeTransformationMatrix_Tabulated(uint64_t heapID, out mat3 mat)
{
    leb__IdentityMatrix3x3(mat);

    const uint64_t msb = 1ul << LEB_TABLE_DEPTH;
    const uint64_t mask = ~(~0ul << LEB_TABLE_DEPTH);
    while (heapID > mask) {
        uint index = uint((heapID & mask) | msb);
        mat = mat * g_MatrixCache[index];
        heapID >>= LEB_TABLE_DEPTH;
    }
    mat = mat * g_MatrixCache[uint(heapID)];
}

void leb__DecodeTransformationMatrix_Tabulated(uint64_t heapID, out dmat3 mat)
{
    uint64_t parentHeapID = heapID / 2ul;
    const uint msb = 1u << LEB_TABLE_DEPTH;
    const uint mask = ~(~0u << LEB_TABLE_DEPTH);
    mat3 m1;
    mat3 m2;
    leb__IdentityMatrix3x3(m1);
    leb__IdentityMatrix3x3(m2);

    while ((heapID > uint64_t(mask)) && (heapID > 0x00000000fffffffful)) {
        uint index = uint((heapID & uint64_t(mask)) | uint64_t(msb));
        m1 = m1 * g_MatrixCache[index];
        heapID >>= LEB_TABLE_DEPTH;
    }

    while (heapID > uint64_t(mask)) {
        uint index = uint((heapID & uint64_t(mask)) | uint64_t(msb));
        m2 = m2 * g_MatrixCache[index];
        heapID >>= LEB_TABLE_DEPTH;
    }
    m2 = m2 * g_MatrixCache[uint(heapID)];

    mat = dmat3(m1) * dmat3(m2);
}
#endif

void leb__DecodeTransformationMatrix_parent_child(uint64_t heapID, out mat3 parent, out mat3 child)
{
    int depth = int(leb_depth(heapID));
    leb__IdentityMatrix3x3(parent);

    int bitID;
    for (bitID = depth - 1; bitID > 0; --bitID)
        leb__SplittingMatrix(parent, leb__GetBitValue(heapID, int64_t(bitID)));

    child = depth > 0 ? leb__SplittingMatrix_out(parent, leb__GetBitValue(heapID, int64_t(bitID))) : parent;
}

void leb__DecodeTransformationMatrix_parent_child(uint64_t heapID, out dmat3 parent, out dmat3 child)
{
    int depth = int(leb_depth(heapID));
    leb__IdentityMatrix3x3(parent);

    int bitID;
    for (bitID = depth - 1; bitID > 0; --bitID)
        leb__SplittingMatrix(parent, leb__GetBitValue(heapID, int64_t(bitID)));

    child = leb__SplittingMatrix_out(parent, leb__GetBitValue(heapID, int64_t(bitID)));
}

void leb__DecodeTransformationMatrix_parent_child_optimized(uint64_t heapID, out dmat3 parent, out dmat3 child)
{
    int depth = int(leb_depth(heapID));
    mat3 p0;
    leb__IdentityMatrix3x3(p0);

    int firstStep = max(depth - 1 - 32, 0);
    int bitID;
    for (bitID = depth - 1; bitID > firstStep; --bitID)
        leb__SplittingMatrix(p0, leb__GetBitValue(heapID, int64_t(bitID)));

    mat3 p1;
    leb__IdentityMatrix3x3(p1);
    for (bitID = firstStep; bitID > 0; --bitID)
        leb__SplittingMatrix(p1, leb__GetBitValue(heapID, int64_t(bitID)));

    dmat3 p0_d = dmat3(p0);
    dmat3 p1_d = dmat3(p1);
    parent = p1_d * p0_d;
    child = depth > 0 ? leb__SplittingMatrix_out(parent, leb__GetBitValue(heapID, 0l)) : parent;
}

#ifdef LEB_MATRIX_SET
void leb__DecodeTransformationMatrix_parent_child_Tabulated(uint64_t heapID, out mat3 parent, out mat3 child)
{
    int depth = int(leb_depth(heapID));
    leb__IdentityMatrix3x3(parent);

    const uint64_t msb = 1ul << LEB_TABLE_DEPTH;
    const uint64_t mask = ~(~0ul << LEB_TABLE_DEPTH);
    uint64_t parentHeapID = heapID / 2ul;
    while (parentHeapID > mask) {
        uint index = uint((parentHeapID & mask) | msb);
        parent = parent * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }
    if (parentHeapID != 0ul)
        parent = parent * g_MatrixCache[uint(parentHeapID)];

    child = depth > 0 ? leb__SplittingMatrix_out(parent, leb__GetBitValue(heapID, 0l)) : parent;
}

void leb__DecodeTransformationMatrix_parent_child_Tabulated(uint64_t heapID, out dmat3 parent, out dmat3 child)
{
    int depth = int(leb_depth(heapID));
    uint64_t parentHeapID = heapID / 2ul;
    const uint msb = 1u << LEB_TABLE_DEPTH;
    const uint mask = ~(~0u << LEB_TABLE_DEPTH);
    mat3 m1;
    mat3 m2;
    leb__IdentityMatrix3x3(m1);
    leb__IdentityMatrix3x3(m2);

    while ((parentHeapID > uint64_t(mask)) && (parentHeapID > 0x00000000fffffffful)) {
        uint index = uint((parentHeapID & uint64_t(mask)) | uint64_t(msb));
        m1 = m1 * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }

    while (parentHeapID > uint64_t(mask)) {
        uint index = uint((parentHeapID & uint64_t(mask)) | uint64_t(msb));
        m2 = m2 * g_MatrixCache[index];
        parentHeapID >>= LEB_TABLE_DEPTH;
    }
    if (parentHeapID != 0ul)
        m2 = m2 * g_MatrixCache[uint(parentHeapID)];

    parent = dmat3(m1) * dmat3(m2);
    child = depth > 0 ? leb__SplittingMatrix_out(parent, leb__GetBitValue(heapID, 0l)) : parent;
}
#endif

void leb_DecodeNodeAttributeArray(uint64_t heapID, inout vec3 attributeArray[2])
{
    mat3 m;
    leb__DecodeTransformationMatrix(heapID, m);
    for (int i = 0; i < 2; ++i) {
        vec3 attributeVector = attributeArray[i];
        attributeArray[i][0] = dot(vec3(m[0][0], m[1][0], m[2][0]), attributeVector);
        attributeArray[i][1] = dot(vec3(m[0][1], m[1][1], m[2][1]), attributeVector);
        attributeArray[i][2] = dot(vec3(m[0][2], m[1][2], m[2][2]), attributeVector);
    }
}

void leb_DecodeNodeAttributeArray(uint64_t heapID, inout dvec3 attributeArray[2])
{
    dmat3 m;
    leb__DecodeTransformationMatrix(heapID, m);
    for (int i = 0; i < 2; ++i) {
        dvec3 attributeVector = attributeArray[i];
        attributeArray[i][0] = dot(dvec3(m[0][0], m[1][0], m[2][0]), attributeVector);
        attributeArray[i][1] = dot(dvec3(m[0][1], m[1][1], m[2][1]), attributeVector);
        attributeArray[i][2] = dot(dvec3(m[0][2], m[1][2], m[2][2]), attributeVector);
    }
}

void leb_DecodeNodeAttributeArray(uint64_t heapID, inout vec3 attributeArray[3])
{
    mat3 m;
#ifdef LEB_MATRIX_SET
    leb__DecodeTransformationMatrix_Tabulated(heapID, m);
#else
    leb__DecodeTransformationMatrix(heapID, m);
#endif
    for (int i = 0; i < 3; ++i) {
        vec3 attributeVector = attributeArray[i];
        attributeArray[i][0] = dot(vec3(m[0][0], m[1][0], m[2][0]), attributeVector);
        attributeArray[i][1] = dot(vec3(m[0][1], m[1][1], m[2][1]), attributeVector);
        attributeArray[i][2] = dot(vec3(m[0][2], m[1][2], m[2][2]), attributeVector);
    }
}

void leb_DecodeNodeAttributeArray(uint64_t heapID, inout dvec3 attributeArray[3])
{
    dmat3 m;
#ifdef LEB_MATRIX_SET
    leb__DecodeTransformationMatrix_Tabulated(heapID, m);
#else
    leb__DecodeTransformationMatrix(heapID, m);
#endif
    for (int i = 0; i < 3; ++i) {
        dvec3 attributeVector = attributeArray[i];
        attributeArray[i][0] = dot(dvec3(m[0][0], m[1][0], m[2][0]), attributeVector);
        attributeArray[i][1] = dot(dvec3(m[0][1], m[1][1], m[2][1]), attributeVector);
        attributeArray[i][2] = dot(dvec3(m[0][2], m[1][2], m[2][2]), attributeVector);
    }
}

void leb_DecodeNodeAttributeArray_parent_child(uint64_t heapID, inout vec3 childAttribute[3], out vec3 parentAttribute[3])
{
    mat3 child;
    mat3 parent;
#ifdef LEB_MATRIX_SET
    leb__DecodeTransformationMatrix_parent_child_Tabulated(heapID, parent, child);
#else
    leb__DecodeTransformationMatrix_parent_child(heapID, parent, child);
#endif

    int i;
    for (i = 0; i < 3; ++i) {
        vec3 attributeVector = childAttribute[i];
        parentAttribute[i][0] = dot(vec3(parent[0][0], parent[1][0], parent[2][0]), attributeVector);
        parentAttribute[i][1] = dot(vec3(parent[0][1], parent[1][1], parent[2][1]), attributeVector);
        parentAttribute[i][2] = dot(vec3(parent[0][2], parent[1][2], parent[2][2]), attributeVector);
    }

    for (i = 0; i < 3; ++i) {
        vec3 attributeVector = childAttribute[i];
        childAttribute[i][0] = dot(vec3(child[0][0], child[1][0], child[2][0]), attributeVector);
        childAttribute[i][1] = dot(vec3(child[0][1], child[1][1], child[2][1]), attributeVector);
        childAttribute[i][2] = dot(vec3(child[0][2], child[1][2], child[2][2]), attributeVector);
    }
}

void leb_DecodeNodeAttributeArray_parent_child(uint64_t heapID, inout dvec3 childAttribute[3], out dvec3 parentAttribute[3])
{
    dmat3 child;
    dmat3 parent;
#ifdef LEB_MATRIX_SET
    leb__DecodeTransformationMatrix_parent_child_Tabulated(heapID, parent, child);
#else
    leb__DecodeTransformationMatrix_parent_child_optimized(heapID, parent, child);
#endif

    int i;
    for (i = 0; i < 3; ++i) {
        dvec3 attributeVector = childAttribute[i];
        parentAttribute[i][0] = dot(dvec3(parent[0][0], parent[1][0], parent[2][0]), attributeVector);
        parentAttribute[i][1] = dot(dvec3(parent[0][1], parent[1][1], parent[2][1]), attributeVector);
        parentAttribute[i][2] = dot(dvec3(parent[0][2], parent[1][2], parent[2][2]), attributeVector);
    }

    for (i = 0; i < 3; ++i) {
        dvec3 attributeVector = childAttribute[i];
        childAttribute[i][0] = dot(dvec3(child[0][0], child[1][0], child[2][0]), attributeVector);
        childAttribute[i][1] = dot(dvec3(child[0][1], child[1][1], child[2][1]), attributeVector);
        childAttribute[i][2] = dot(dvec3(child[0][2], child[1][2], child[2][2]), attributeVector);
    }
}

#endif // LEB_GLSL
