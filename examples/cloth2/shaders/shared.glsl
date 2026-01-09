#ifndef SHARED_GLSL
#define SHARED_GLSL

#define EPSILON 1e-6
#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559
#define COLLISION_MARGIN 0.01

struct VertexOffsets{
    int firstIndex;
    int vertexOffset;
    int material;
    int padding1;
};

struct Vertex{
    vec4 position;
    vec4 color;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
};

layout(push_constant) uniform SIM_CONSTANTS {
    vec2 inv_cloth_size;
    float timeStep;
    float mass;
    float ksStruct;
    float ksShear;
    float ksBend;
    float kdStruct;
    float kdShear;
    float kdBend;
    float kd;
    float elapsedTime;
    int simWind;
    float gravityY;
    float windStrength;
    float windSpeed;
    int collider;
    int numPoints;
    int _pinCorners;
    int numConstraints;
    int constraintOffset;
    int solveType;
    float jacobiScale;
};

bool pinCorners() { return _pinCorners == 1; }

vec3 gravity = vec3(0, gravityY, 0);
int width = int(sqrt(numPoints));
int height = int(width);
ivec2 size = ivec2(gl_WorkGroupSize * gl_NumWorkGroups);
ivec2 gid = ivec2(gl_GlobalInvocationID);
int id = gid.y * width + gid.x;

bool outOfBounds = (gid.x >= width || gid.y >= height);

ivec2 neighbourIndices[12] = {
ivec2(0, 1), ivec2(1, 0), ivec2(0, -1), ivec2(-1, 0),  // structural neigbhours
ivec2(-1, 1), ivec2(1, 1), ivec2(-1, -1), ivec2(1, -1),  // shear neigbhours
ivec2(0, 2), ivec2(0, -2), ivec2(-2, 0), ivec2(2, 0)    // bend neigbhours
};

bool neighbour(int i, out int nid, out ivec2 coord){
    coord = neighbourIndices[i];
    ivec2 index =  coord + gid;
    if(index.x < 0 || index.x >= width || index.y < 0 || index.y >= height){
        return false;
    }
    nid = index.y * width + index.x;
    return true;
}

float sdBox( vec3 p){
    vec3 q = abs(p) - vec3(1 + COLLISION_MARGIN);
    return length(max(q,0.0)) + min(max(q.x,max(q.y,q.z)),0.0);
}

vec3 boxNormal(vec3 p) {
    vec3 pp = abs(p);
    int axis = 0;
    for(int i = 1; i < 3; i++){
        if(pp[i] > pp[axis]) {
            axis = i;
        }
    }

    vec3 N = vec3(0);
    N[axis] = sign(p[axis]);
    vec3 pN = normalize(p);

    //    if(abs(dot(N, pN)) <= 1e-4) {
    //        N = pN;
    //    }
    return N;
}

vec3 offsetRay(in vec3 p, in vec3 n)
{
    const float intScale   = 256.0f;
    const float floatScale = 1.0f / 65536.0f;
    const float origin     = 1.0f / 32.0f;

    ivec3 of_i = ivec3(intScale * n.x, intScale * n.y, intScale * n.z);

    vec3 p_i = vec3(intBitsToFloat(floatBitsToInt(p.x) + ((p.x < 0) ? -of_i.x : of_i.x)),
    intBitsToFloat(floatBitsToInt(p.y) + ((p.y < 0) ? -of_i.y : of_i.y)),
    intBitsToFloat(floatBitsToInt(p.z) + ((p.z < 0) ? -of_i.z : of_i.z)));

    return vec3(abs(p.x) < origin ? p.x + floatScale * n.x : p_i.x, //
    abs(p.y) < origin ? p.y + floatScale * n.y : p_i.y, //
    abs(p.z) < origin ? p.z + floatScale * n.z : p_i.z);
}

vec3 closestPointOnPlane(vec3 p, vec3 N, float d) {
    float t = dot(N, p) - d;
    return p - t * N;
}


#endif // SHARED_GLSL