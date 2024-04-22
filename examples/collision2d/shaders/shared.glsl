#ifndef SHARED_GLSL
#define SHARED_GLSL

#define PI 3.1415926535897932384626433832795
#define SQRT2 1.4142135623730950488016887242097
#define D_BITS 2
#define _2D_BITS (1 << D_BITS)

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

layout(set = 0, binding = 0) buffer Globals {
    mat4 projection;
    Domain domain;
    float gravity;
    float spacing;
    float halfSpacing;
    float time;
    int numObjects;
    int numCells;
    int screenWidth;
    int screenHeight;
    int segmentSize;
} global;

uvec2 dimensions() {
    return uvec2(ceil((global.domain.upper - global.domain.lower)/global.spacing));
}

uint hash(uvec2 pid) {
    uvec2 dim = dimensions();
    return pid.y * dim.x + pid.x;
}

ivec2 intCoord(vec2 pos) {
    return ivec2(pos/global.spacing);
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

void addHomeCellToControlBits(ivec2 cell, inout uint controlBits) {
    uint cellType = (cell.x % 2) + (cell.y % 2) * 2;
    controlBits = controlBits | cellType;
}

void addIntersectingCelltoControlBits(ivec2 cell, inout uint controlBits) {
    uint cellType = (cell.x % 2) + (cell.y % 2) * 2;
    uint bits = 1 << cellType;
    controlBits = controlBits | (bits << D_BITS);
}

#endif // SHARED_GLSL