#ifndef COMMON_GLSL
#define COMMON_GLSL

#include "random.glsl"
#include "util.glsl"
#include "fresnel.glsl"

#define SHAPE_RECTANGLE 0
#define SHAPE_SPHERE 1
#define SHAPE_DISK 2

#define LIGHT_FLAG_DELTA_POSITION 0x1u
#define LIGHT_FLAG_DELTA_DIRECTION 0x2u
#define LIGHT_FLAG_DELTA_AREA 0x4u
#define LIGHT_FLAG_INFINITE  0x8u
#define LIGHT_FLAG_HAS_PRIMITIVE = 0x16u

struct ShapeRef{
    int objectId;
    int shapeId;
    int shape;
    int padding;
};

struct Rectangle{
    vec3 p0;
    vec3 p1;
    vec3 p2;
    vec3 p3;
};

struct Sphere{
    vec3 center;
    float radius;
};

struct Disk{
    vec3 center;
    float radius;
    float height;
};

struct Polygon{
    int instanceId;
    int numTriangles;
    int triangleOffset;
    float area;
};

struct Ray{
    vec3 origin;
    vec3 direction;
    vec3 transmission;
    int medium;
};

struct Light{
    vec3 position;
    uint flags;

    vec3 normal;
    int shapeType;

    vec3 value;
    int shapeId;

    float cosWidth;
    float fallOffStart;
    int envMapId;
};

float area(Rectangle rectangle){
    vec3 e0 = rectangle.p1 - rectangle.p0;
    vec3 e1 = rectangle.p2 - rectangle.p0;

    return length(e0) * length(e1);
}

float area(Sphere sphere){
    return 4 * PI * sphere.radius * sphere.radius;
}

float area(Disk disk){
    return PI * disk.radius * disk.radius;
}

bool isPositional(Light light){
    return (light.flags & LIGHT_FLAG_DELTA_POSITION) == LIGHT_FLAG_DELTA_POSITION;
}

bool isArea(Light light){
    return (light.flags & LIGHT_FLAG_DELTA_AREA) == LIGHT_FLAG_DELTA_AREA;
}

bool isDistant(Light light){
    return (light.flags & LIGHT_FLAG_DELTA_DIRECTION) == LIGHT_FLAG_DELTA_DIRECTION;
}

bool isInfinite(Light light){
    return (light.flags & LIGHT_FLAG_INFINITE) == LIGHT_FLAG_INFINITE;
}

struct SurfaceDataParam{
    mat4x3 objToWorld;
    vec3 albedo;
    vec3 emission;
    vec3 x; // hit point on surface
    vec3 gn; // geometry normal of hit point
    vec3 sn; // shading normal;
    vec3 wo;
    float u;
    float v;
    float w;
    float metalness;
    float roughness;
    int instanceId;
    int vertexOffsetId;
    int primitiveId;
};

struct Surface{
    vec3 albedo;
    vec3 emission;
    vec3 x;
    vec3 gN;
    vec3 sN;
    float roughness;
    float metalness;
    float opacity;
    int id;
    bool inside;
    bool volume;
};

bool isMirror(SurfaceDataParam surface){
    return surface.metalness == 1 && surface.roughness == 0;
}

bool isMirror(Surface surface){
    return surface.metalness == 1 && surface.roughness == 0;
}

struct LightInfo{
    mat4x3 objToWorld;
    vec3 radiance;
    vec3 value;
    vec3 x;     // point on light
    vec3 n;     // light normal
    vec3 sx;    // point on surface
    vec3 sn;    // surface normal;
    vec3 wi;
    float dist;
    float NdotL;
    RngStateType rngState;
    int id;
    float pdf;
    float area;
    uint flags;
};

struct Material{
    vec3 diffuse;
    vec3 ambient;
    vec3 metalness;
    vec3 emission;
    vec3 transmittance;
    float roughness;
    float ior;
    float opacity;
    float illum;
};

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

struct BrdfArgs{
    // input
    vec3 wo;
    vec3 surfacePoint;
    vec3 surfaceNormal;
    vec3 surfaceGeomNormal;
    vec3 surfaceAlbedo;
    vec3 lightPoint;
    float surfaceMetalness;
    float surfaceRoughness;
    int diffuseType;
    int specularType;
    int ndfFunc;
    int brdfType;
    RngStateType rngState;

    // output
    vec3 combinedBrdf;
    vec3 brdfWeight;
    vec3 wi;
};

struct BrdfData{
    vec3 specularF0;
    vec3 diffuseReflectance;
    vec3 F; //< Fresnel term
    vec3 V; //< Direction to viewer (or opposite direction of incident ray)
    vec3 sN; //< Shading normal
    vec3 gN;
    vec3 H; //< Half vector (microfacet normal)
    vec3 L; //< Direction to light (or direction of reflecting ray)

    float roughness;    //< perceptively linear roughness (artist's input)
    float alpha;        //< linear roughness - often 'alpha' in specular BRDF equations
    float alphaSquared; //< alpha squared - pre-calculated value commonly used in BRDF equations

    float NdotL;
    float NdotV;

    float LdotH;
    float NdotH;
    float VdotH;

// True when V/L is backfacing wrt. shading normal N
    bool Vbackfacing;
    bool Lbackfacing;
    int brdfType;
};

vec2 randomVec2(inout RngStateType rngState){
    return vec2(rand(rngState), rand(rngState));
}

struct Sample{
    int i;
    int N;
};

struct HitData {
    Ray ray;
    Sample _sample;
    Surface surface;
    vec3 brdfWeight;
    vec3 lightContribution;
    vec3 transmission;
    RngStateType rngState;
    bool hit;
};

void reset(inout HitData hitData){
    hitData.ray.origin = vec3(0);
    hitData.ray.direction = vec3(0);
    hitData.ray.transmission = vec3(1);
    hitData.ray.medium = -1;
    hitData.surface.volume = false;
    hitData.surface.inside = false;
    hitData.surface.emission = vec3(0);
    hitData.brdfWeight = vec3(1);
    hitData.lightContribution = vec3(0);
    hitData.transmission = vec3(1);
    hitData.hit = false;
}

HitData createHitData(RngStateType rngState){
    HitData hitData;
    reset(hitData);
    hitData.rngState = rngState;
    return hitData;
}

struct SurfaceRef{
    mat4x3 objToWorld;
    vec2 attribs;
    int instanceId;
    int vertexOffsetId;
    int primitiveId;
};

struct OcclusionData{
    Ray ray;
    vec3 transmission;
    bool Continue;
    bool isShadowed;
};

#endif // COMMON_GLSL