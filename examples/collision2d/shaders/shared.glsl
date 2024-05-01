#ifndef SHARED_GLSL
#define SHARED_GLSL

#define PI 3.1415926535897932384626433832795
#define SQRT2 1.4142135623730950488016887242097
#define TWO_DIMENSIONS 2u
#define D_BITS TWO_DIMENSIONS
#define HOME_CELL_MASK ((1u << D_BITS) - 1u)
#define INTERSECTING_CELLS_MASK ((1u << (1u << D_BITS)) - 1u)
#define HOME_CELL_TYPE(ctrlBits)  (1u << (ctrlBits & HOME_CELL_MASK))
#define INTERSECTING_CELLS(ctrlBits) ((ctrlBits >> D_BITS) & INTERSECTING_CELLS_MASK)
#define SHARE_COMMON_CELLS(A, B) ((INTERSECTING_CELLS(A) & INTERSECTING_CELLS(B)) != 0)
#define CELL_TYPE_INDEX(X, Y) ((X % 2u) + (Y % 2u) * 2u)
#define CELL_TYPE_INDEX_3D(X, Y, Z) ((x % 2u) + (Y % 2u) * 2u + (Z % 2u) * 4u)

struct Domain {
    vec2 lower;
    vec2 upper;
};

struct Bounds {
    vec2 min;
    vec2 max;
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
    uint padding;
};

struct UpdateInfo {
    uint objectId;
    uint pass;
    uint tid;
    uint cellID;
};


layout(set = 0, binding = 0) buffer Globals {
    mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float radius;
    float time;
    uint numObjects;
    uint gridSize;
    uint numCells;
    uint segmentSize;
    uint numCellIndices;
    uint numEmitters;
    uint numUpdates;
    uint frame;
    uint screenWidth;
    uint screenHeight;
} global;

layout(set = 0, binding = 1) buffer Updates {
    UpdateInfo updates[];
};

uvec2 dimensions() {
    return uvec2(((global.domain.upper - global.domain.lower)/global.spacing) + 1);
}

uint hash(uvec2 pid) {
    uvec2 dim = dimensions();
    return pid.y * dim.x + pid.x;
}

ivec2 intCoord(vec2 pos) {
    return ivec2(pos/global.spacing);
}


uvec2 uintCoord(vec2 pos) {
    return uvec2(pos/global.spacing);
}

vec2 coordinate(uint cellID) {
    uvec2 dim = dimensions();
    return vec2( cellID % dim.x, cellID / dim.x );
}

uint computeHash(vec2 pos) {
    uvec2 pid = intCoord(pos);
    return hash(pid);
}

void swap(inout float a, float b) {
    float temp = a;
    a = b;
    b = temp;
}

Bounds createBounds(vec2 center, float radius) {
    Bounds bounds;
    bounds.min = center - radius;
    bounds.max = center + radius;
    return bounds;
}

bool test(Bounds a, Bounds b) {
    bvec2 overlap = bvec2(false);
    for(int axis = 0; axis < 2; axis++){
        float minA = a.min[axis];
        float minB = b.min[axis];
        float maxA = a.max[axis];
        float maxB = b.max[axis];

        if(minA > minB) {
            overlap[axis] = minA < maxB;
        }else {
            overlap[axis] = minB < maxA;
        }
    }
    return all(overlap);
}

bool test(uvec2 a, uvec2 b) {
    Bounds aBounds = Bounds(vec2(a), vec2(a) + global.spacing);
    Bounds bBounds = Bounds(vec2(b), vec2(b) + global.spacing);
    return test(aBounds, bBounds);
}

void addHomeCellToControlBits(ivec2 cell, inout uint controlBits) {
    uint cellType = CELL_TYPE_INDEX(cell.x, cell.y);
    controlBits = controlBits | cellType;
}

void addIntersectingCelltoControlBits(ivec2 cell, inout uint controlBits) {
    uint cellType = 1 << CELL_TYPE_INDEX(cell.x, cell.y);
    controlBits = controlBits | (cellType << D_BITS);
}

bool isHomeCell(uint cell, uint controlBits) {
    uvec2 dim = dimensions();
    uint cellType =  1 << CELL_TYPE_INDEX(cell % dim.x, cell/dim.x);
    return cellType == HOME_CELL_TYPE(controlBits);
}

bool processCollision(uint passCellType, uint controlBitsA, uint controlBitsB) {
    uint homeCellA = HOME_CELL_TYPE(controlBitsA);
    uint homeCellB = HOME_CELL_TYPE(controlBitsB);
    return SHARE_COMMON_CELLS(controlBitsA, controlBitsA)  && min(homeCellA, homeCellB) == passCellType;
}


uint countCellIntersections(uint controlBits){
    uint ic = INTERSECTING_CELLS(controlBits);
    return uint(sign(ic & 8u) + sign(ic & 4u) + sign(ic & 2u) + sign(ic & 1u));
}

bool test(vec2 position, uint cellHash) {
    vec2 cell = coordinate(cellHash);
    Bounds oBounds = createBounds(position, global.radius * SQRT2);
    Bounds cBounds = Bounds(cell, cell  + global.spacing);

    return test(oBounds, cBounds);
}

// There is an issue with the original processCollision which relies on only control bits
// SHARE_COMMON_CELLS will return true for intersections on both sides of controlBitA
// which means we may get false positives if we test against the wrong side
// so in addition we check if A intersects the Home Cell of B
bool processCollision(uint passCellType, uint controlBitsA, uint controlBitsB, vec2 posA, uint cellHashB) {
    uint homeCellA = HOME_CELL_TYPE(controlBitsA);
    uint homeCellB = HOME_CELL_TYPE(controlBitsB);

    return SHARE_COMMON_CELLS(controlBitsA, controlBitsA) && (min(homeCellA, homeCellB) == passCellType || !test(posA, cellHashB));
}


Domain shrink(Domain bounds, float factor){
    Domain newDomain = bounds;
    newDomain.lower += factor;
    newDomain.upper -= factor;
    
    return newDomain;
}


#endif // SHARED_GLSL