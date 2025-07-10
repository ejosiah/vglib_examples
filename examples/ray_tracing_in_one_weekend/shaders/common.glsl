#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "random.glsl"
#include "sampling.glsl"
#include "rtx_utils.glsl"
#include "raytracing_implicits/implicits.glsl"

const float rayOffset = 0.0001;

struct VertexOffsets{
    int firstIndex;
    int vertexOffset;
    int material;
    int padding1;
};

struct Vertex{
    vec3 position;
    vec3 color;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec2 uv;
};

struct SceneObject{
    mat4 xform;
    mat4 xformIT;
    int objId;
    int padding0;
    int padding1;
    int padding2;
};

struct HitRecord {
    vec3 n;
    vec3 x;
    vec3 wo;
    vec3 wi;
    vec3 color;
    vec3 attenuation;
    RngStateType rngState;
    bool hit;
};


layout(set = 0, binding = 1, scalar) uniform Constants {
    mat4 viewInverse;
    mat4 projInverse;
    uint frame;
    uint maxBounce;
    uint sampleCount;
    uint currentSample;
    int adaptiveSampling;
};

layout(set = 0, binding = 3) buffer VERTEX_BUFFER {
    Vertex v[];
} vertices[];

layout(set = 0, binding = 4) buffer INDEX_BUFFER {
    int i[];
} indices[];

vec2 sampleVec2(inout RngStateType rngState) {
    return vec2(rand(rngState), rand(rngState));
}

bool isBlack(vec3 v) {
    return all(equal(vec3(0), v));
}

float fresnelSchlick(float cosThata, float ior) {
    float r0 = (1 - ior)/(1+ior);
    r0 *= r0;
    return r0 + (1 - r0) * pow((1 - cosThata), 5);
}

void swap(inout float a, inout float b){
    float temp = a;
    a = b;
    b = temp;
}


float fresnel(float cosThetaI, float etaI, float etaT) {
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    // Potentially swap indices of refraction
    bool entering = cosThetaI > 0.f;
    if (!entering) {
        swap(etaI, etaT);
        cosThetaI = abs(cosThetaI);
    }

    // Compute _cosThetaT_ using Snell's law
    float sinThetaI = sqrt(max(0, 1 - cosThetaI * cosThetaI));
    float sinThetaT = etaI / etaT * sinThetaI;

    // Handle total internal reflection
    if (sinThetaT >= 1) {
        return 1;
    }
    float cosThetaT = sqrt(max(0, 1 - sinThetaT * sinThetaT));
    float Rparl = ((etaT * cosThetaI) - (etaI * cosThetaT)) /
    ((etaT * cosThetaI) + (etaI * cosThetaT));
    float Rperp = ((etaI * cosThetaI) - (etaT * cosThetaT)) /
    ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (Rparl * Rparl + Rperp * Rperp) / 2;
}

void getSurfaceInfo(Sphere sphere, vec3 rOrigin, vec3 rDirection, float tHit, out vec3 position, out vec3 normal) {
    position = rOrigin + rDirection * tHit;
    vec3 center = sphere.center;
    normal = normalize(position - center);
}

void getSurfaceInfo(vec3 rOrigin, vec3 rDirection, float tHit, mat4x3 xform,  out vec3 position, out vec3 normal) {
    position = rOrigin + rDirection * tHit;
    vec3 center = xform * vec4(0, 0, 0, 1);
    normal = normalize(position - center);
}

void getSurfaceInfo(vec2 attibs, vec3 hitPositions[3], mat4x3 xform, out vec3 position, out vec3 normal) {
    vec3 p0 = xform * vec4(hitPositions[0], 1);
    vec3 p1 = xform * vec4(hitPositions[1], 1);
    vec3 p2 = xform * vec4(hitPositions[2], 1);

    vec3 e0 = p1 - p0;
    vec3 e1 = p2 - p0;

    normal = normalize(cross(e0, e1));

    float u = 1 - attibs.x - attibs.y;
    float v = attibs.x;
    float w = attibs.y;

    position = p0 * u + p1 * v + p2 * w;
}

void getSurfaceInfo(uint id, uint primitiveId, vec2 attribs, mat4x3 xform, out vec3 position, out vec3 normal) {
    float u = 1 - attribs.x - attribs.y;
    float v = attribs.x;
    float w = attribs.y;


    ivec3 index = ivec3(
        indices[id].i[3 * primitiveId + 0],
        indices[id].i[3 * primitiveId + 1],
        indices[id].i[3 * primitiveId + 2]
    );

    Vertex v0 = vertices[id].v[index.x];
    Vertex v1 = vertices[id].v[index.y];
    Vertex v2 = vertices[id].v[index.z];

    position = v0.position * u + v1.position * v + v2.position * w;
    normal = v0.normal * u + v1.normal * v + v2.normal * w;

    position = xform * vec4(position, 1);
//    normal = inverse(transpose(mat3(xform))) * normal;
}

vec3 offsetRayImpl(vec3 p, vec3 n) {
    return p + n * rayOffset;
}

#endif // COMMON_GLSL