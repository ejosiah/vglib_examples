#ifndef SHARED_GLSL
#define SHARED_GLSL

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

struct DistanceConstraint {
    uint a;
    uint b;
    float l;
};

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

struct Emitter {
    vec3 origin;
    vec3 direction;
    float radius;
    float offset;
    float speed;
    float spread;
    int maxNumberOfParticlePerSecond;
    int maxNumberOfParticles;
    float firstFrameTimeInSeconds;
    float currentTime;
    int numberOfEmittedParticles;
    int disabled;
};

struct DebugInfo {
    vec3 center[8];
    vec3 min[8];
    vec3 max[8];
    int overlap[8];
};


layout(set = 0, binding = 0, scalar) buffer Globals {
    Domain domain;
    vec3 gravity;
    vec3 light;
    float spacing;
    float radius;
    float time;
    uint numObjects;
    uint gridSize;
    uint numCells;
    uint segmentSize;
    uint numCellIndices;
    uint numEmitters;
    uint numSphereEmitters;
    uint numUpdates;
    uint frame;
    uint numDistanceConstraints;
    float restitution;
} global;

vec3 remap(vec3 x, vec3 a, vec3 b, vec3 c, vec3 d) {
    return mix(c, d, (x-a)/(b-a));
}

vec3 remap(vec3 p) {
    const vec3 a = global.domain.lower;
    const vec3 b = global.domain.upper;
    const vec3 d = b - a;
    return mix(vec3(0), d, (p-a)/d);
}

Domain shrink(Domain domain, float factor){
    Domain newDomain = domain;
    newDomain.lower += factor;
    newDomain.upper -= factor;

    return newDomain;
}


Domain expand(Domain domain, float factor) {
    Domain newDomain = domain;
    newDomain.lower -= factor;
    newDomain.upper += factor;

    return newDomain;
}

uvec3 dimensions() {
    Domain d = expand(global.domain, global.spacing);
    return uvec3(((d.upper - d.lower)/global.spacing));
}

uint hash(uvec3 pid) {
    uvec3 dim = dimensions();
    return (pid.z * dim.y + pid.y) * dim.x + pid.x;
}

ivec3 intCoord(vec3 pos) {
    return ivec3(pos/global.spacing);
}


uvec3 uintCoord(vec3 pos) {
    return uvec3(pos/global.spacing);
}

vec3 coordinate(uint cellID) {
    uvec3 dim = dimensions();
    return vec3(cellID % dim.x, (cellID/dim.x) % dim.y, cellID / (dim.x * dim.y));
}

uint computeHash(vec3 pos) {
    uvec3 pid = intCoord(pos);
    return hash(pid);
}

void swap(inout float a, float b) {
    float temp = a;
    a = b;
    b = temp;
}

Bounds createBounds(vec3 center, float radius) {
    Bounds bounds;
    bounds.min = center - radius;
    bounds.max = center + radius;
    return bounds;
}

bool test(Bounds a, Bounds b) {
    bvec3 overlap = bvec3(false);
    for(int axis = 0; axis < 3; axis++){
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

bool test(uvec3 a, uvec3 b) {
    Bounds aBounds = Bounds(vec3(a), vec3(a) + global.spacing);
    Bounds bBounds = Bounds(vec3(b), vec3(b) + global.spacing);
    return test(aBounds, bBounds);
}

void addHomeCellToControlBits(ivec3 cell, inout uint controlBits) {
    uint cellType = CELL_TYPE_INDEX(cell.x, cell.y, cell.z);
    controlBits = controlBits | cellType;
}

void addIntersectingCelltoControlBits(ivec3 cell, inout uint controlBits) {
    uint cellType = 1 << CELL_TYPE_INDEX(cell.x, cell.y, cell.z);
    controlBits = controlBits | (cellType << D_BITS);
}

bool isHomeCell(uint cell, uint controlBits) {
    uvec3 dim = dimensions();
    uint cellType =  1 << CELL_TYPE_INDEX(cell % dim.x, (cell/dim.x) % dim.y, cell / (dim.x * dim.y));
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

bool test(vec3 position, uint cellHash) {
    vec3 cell = coordinate(cellHash);
    Bounds oBounds = createBounds(position, global.radius * SQRT2);
    Bounds cBounds = Bounds(cell, cell  + global.spacing);

    return test(oBounds, cBounds);
}

// There is an issue with the original processCollision which relies on only control bits
// SHARE_COMMON_CELLS will return true for intersections on both sides of controlBitA
// which means we may get false positives if we test against the wrong side
// so in addition we check if A intersects the Home Cell of B
bool processCollision(uint passCellType, uint controlBitsA, uint controlBitsB, vec3 posA, uint cellHashB) {
    uint homeCellA = HOME_CELL_TYPE(controlBitsA);
    uint homeCellB = HOME_CELL_TYPE(controlBitsB);

    return SHARE_COMMON_CELLS(controlBitsA, controlBitsA) && (min(homeCellA, homeCellB) == passCellType || !test(posA, cellHashB));
}

vec3 hsv_to_rgb(float h, float s, float v) {
    float h_i = floor(h*6.);
    float f = fract(h*6.);
    float p = v * (1. - s);
    float q = v * (1. - f*s);
    float t = v * (1. - (1. - f) * s);

    if(h_i == 0.) {
        return vec3(v, t, p);
    }else if (h_i == 1.) {
        return vec3(q, v, p);
    }else if(h_i == 2.) {
        return vec3(p, v, t);
    }else if(h_i == 3.) {
        return vec3(p, q, v);
    }else if(h_i == 4.) {
        return vec3(t, p, v);
    }else  {
        return vec3(v, p, q);
    }
}

float hash11(float p){
    p = fract(p * .1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

#endif // SHARED_GLSL