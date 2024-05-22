#ifndef SHARED_GLSL
#define SHARED_GLSL

#define EPSILON 1e-6
#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559

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
};

vec3 gravity = vec3(0, gravityY, 0);
int width = int(gl_WorkGroupSize.x * gl_NumWorkGroups.x);
int height = int(gl_WorkGroupSize.y * gl_NumWorkGroups.y);
int numPoints = height * width;
int id = int(gl_GlobalInvocationID.y * width + gl_GlobalInvocationID.x);

ivec2 neighbourIndices[12] = {
ivec2(0, 1), ivec2(1, 0), ivec2(0, -1), ivec2(-1, 0),  // structural neigbhours
ivec2(-1, 1), ivec2(1, 1), ivec2(-1, -1), ivec2(1, -1),  // shear neigbhours
ivec2(0, 2), ivec2(0, -2), ivec2(-2, 0), ivec2(2, 0)    // bend neigbhours
};

bool neighbour(int i, out int nid, out ivec2 coord){
    coord = neighbourIndices[i];
    ivec2 index =  coord + ivec2(gl_GlobalInvocationID.xy);
    if(index.x < 0 || index.x >= width || index.y < 0 || index.y >= height){
        return false;
    }
    nid = index.y * width + index.x;
    return true;
}



#endif // SHARED_GLSL