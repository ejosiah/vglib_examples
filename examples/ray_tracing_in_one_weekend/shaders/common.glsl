#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "random.glsl"
#include "sampling.glsl"
#include "rtx_utils.glsl"
#include "raytracing_implicits/implicits.glsl"
#include "triplaner_mapping.glsl"

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
    vec3 emission;
    bool inside_medium;
    int bounce;
    RngStateType rngState;
    uint seed;
    bool hit;
};

struct Medium {
    vec3 albedo;
    float density;
};

layout(set = 0, binding = 1, scalar) uniform Constants {
    mat4 viewInverse;
    mat4 projInverse;
    vec3 cameraPosition;
    uint frame;
    uint maxBounce;
    uint sampleCount;
    uint currentSample;
    float apertureSize;
    float focalDistance;
    int adaptiveSampling;
    int blueNoise;
    int litBackGround;
};

bool adaptiveSamplingEnabled() {
    return adaptiveSampling == 1;
}

bool useBlueNoise() {
    return blueNoise == 1;
}

layout(set = 0, binding = 4) uniform sampler2DArray noise_texture;
layout(set = 1, binding = 10) uniform sampler2D global_textures[];
layout(set = 1, binding = 10) uniform sampler3D global_textures3d[];

vec2 sampleVec2(inout RngStateType rngState) {
    return vec2(rand(rngState), rand(rngState));
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

vec3 offsetRayImpl(vec3 p, vec3 n) {
    return p + n * rayOffset;
}

vec2 sampleNoiseBlue(inout uint seed) {
    vec3 tSize = textureSize(noise_texture, 0);
    float layer = mod(frame + seed, tSize.z);
    vec2 numTiles = vec2(gl_LaunchSize)/tSize.xy;
    vec2 uv = vec2(gl_LaunchID)/vec2(gl_LaunchSize);
    vec2 tileUV = fract(uv * numTiles);

    ++seed;
    return texture(noise_texture, vec3(tileUV, layer)).xy;
}

vec2 sampleVec2(inout HitRecord hRec) {
    if(useBlueNoise()) {
        return sampleNoiseBlue(hRec.seed);
    }else {
        return sampleVec2(hRec.rngState);
    }
}

vec3 sampleVec3(inout HitRecord hRec) {
    return vec3(rand(hRec.rngState), rand(hRec.rngState), rand(hRec.rngState));
}

float sampleReal(inout HitRecord hRec) {
    if(useBlueNoise()) {
        return sampleNoiseBlue(hRec.seed).x;
    }else {
        return rand(hRec.rngState);
    }
}

void generateCameraRay(
    vec2 screenUV,              // Input: normalized screen coordinates [0,1]
    vec2 lensUV,                // Input: uniform sample on [0,1]² for aperture jitter
    float focalLength,          // Input: focal distance (scene units)
    float apertureSize,         // Input: aperture radius (scene units)
    mat4 invProjMatrix,         // Input: inverse projection matrix
    mat4 invViewMatrix,         // Input: inverse view matrix
    inout vec3 rayOriginWorld,    // Output: ray origin in world space
    inout vec3 rayDirectionWorld  // Output: ray direction in world space
) {
    // Convert screenUV to normalized device coordinates [-1, 1]
    vec2 ndc = screenUV * 2.0 - 1.0;
    vec4 clipPos = vec4(ndc, -1.0, 1.0); // z = -1 for near plane, TODO change this to z = 0 (vulkan)

    // Transform from clip space to view space
    vec4 viewPos = invProjMatrix * clipPos;
    viewPos /= viewPos.w;
    vec3 rayDirView = normalize(viewPos.xyz);

    // Transform view direction to world space
    vec3 rayDirWorld = normalize((invViewMatrix * vec4(rayDirView, 0.0)).xyz);
    vec3 camPosWorld = (invViewMatrix * vec4(0.0, 0.0, 0.0, 1.0)).xyz;

    // Sample aperture offset in view space (concentric disk)
    vec2 lensSample = concentricSampleDisk(lensUV) * apertureSize * 0.5;
    vec3 apertureOffsetView = vec3(lensSample, 0.0);
    vec3 apertureOffsetWorld = (invViewMatrix * vec4(apertureOffsetView, 0.0)).xyz;

    // Final ray origin is offset on the lens
    rayOriginWorld = camPosWorld + apertureOffsetWorld;

    // Focal point in world space
    vec3 focalPoint = camPosWorld + rayDirWorld * focalLength;

    // Final ray direction towards focal point
    rayDirectionWorld = normalize(focalPoint - rayOriginWorld);
}

#endif // COMMON_GLSL