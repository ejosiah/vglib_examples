#ifndef HASH_GRID_3D_SHARED_GLSL
#define HASH_GRID_3D_SHARED_GLSL

#extension GL_EXT_scalar_block_layout : enable

#define PI 3.1415926535897932384626433832795
#define SQRT2 1.4142135623730950488016887242097
#define THREE_DIMENSIONS 3u
#define D_BITS THREE_DIMENSIONS
#define HOME_CELL_MASK ((1u << D_BITS) - 1u)
#define INTERSECTING_CELLS_MASK ((1u << (1u << D_BITS)) - 1u)
#define HOME_CELL_TYPE(ctrlBits)  (1u << (ctrlBits & HOME_CELL_MASK))
#define INTERSECTING_CELLS(ctrlBits) ((ctrlBits >> D_BITS) & INTERSECTING_CELLS_MASK)
#define SHARE_COMMON_CELLS(A, B) ((INTERSECTING_CELLS(A) & INTERSECTING_CELLS(B)) != 0)
#define CELL_TYPE_INDEX(X, Y, Z) ((X % 2u) + (Y % 2u) * 2u + (Z % 2u) * 4u)
#define PREVIOUS 0
#define CURRENT 1

struct Domain {
    vec3 lower;
    vec3 upper;
};

struct Bounds {
    vec3 min;
    vec3 max;
};

struct Attribute {
    uint objectID;
    uint controlBits;
};

struct CellInfo {
    uint index;
    uint numHomeCells;
    uint numPhantomCells;
    uint numCells;
};

struct DispatchCommand {
    uint gx;
    uint gy;
    uint gz;
};

layout(set = 0, binding = 1, scalar) buffer Args {
    Domain domain;
    uint numObjects;
} args;

layout(set = 0, binding = 1, scalar) buffer Globals {
    float spacing;
    uint gridSize;
    uint numCells;
    uint segmentSize;
    uint numCellIndices;
} global;

#endif// HASH_GRID_3D_SHARED_GLSL