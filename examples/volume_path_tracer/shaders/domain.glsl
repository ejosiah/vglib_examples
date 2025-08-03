#ifndef VOLUME_PATH_TRACER_DOMAIN_GLSL
#define VOLUME_PATH_TRACER_DOMAIN_GLSL

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing_position_fetch : enable

#include "constants.glsl"
#include "ray_tracing_lang.glsl"
#include "random.glsl"
#include "sampling.glsl"
#include "rtx_utils.glsl"
#include "octahedral.glsl"
#include "path_tracing/eval_brdf.glsl"
#include "path_tracing/medium.glsl"

struct ObjectInfo {
    int materialId;
    int mediumId;
};

struct Material {
    vec3 diffuse;
    float metalness;
    float roughness;
    int bsdf;
};

struct Medium {
    vec3 scattering;
    vec3 absorption;
    vec3 extinction;
    float g;
    int type;
};

struct SurfaceInteraction {
    vec3 x;
    vec3 gN;
    vec3 sN;
    int material;
    int medium;
    bool isFrontFacing;
};

struct MediumInteraction {
    vec3 x;
    vec3 wi;
    bool isValid;
};

struct HitRecord {
    SurfaceInteraction isect;
    vec3 x;
    vec3 wi;
    vec3 wo;
    RngStateType rngState;
    float t;
};

layout(set = 0, binding = 1, scalar) uniform Uniforms {
    mat4 viewInverse;
    mat4 projInverse;
    uint currentSample;
    uint sampleCount;
    uint maxBounce;
    uint frame;
    uint RayCount;
    uint environment;
};

layout(set = 2, binding = 0, scalar) buffer ObjectSSBO {
    ObjectInfo objects[];
};

layout(set = 2, binding = 1, scalar) buffer MaterialSSBO {
    Material materials[];
};

layout(set = 2, binding = 2, scalar) buffer MediumSSBO {
    Medium mediums[];
};

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

void generateRay(inout HitRecord hRec, vec2 id, vec2 screenSize, vec2 offset) {
    const vec2 pixelCenter = id + offset;
    const vec2 screenUV = pixelCenter/screenSize;

    vec2 d = screenUV * 2.0 - 1.0;

    vec4 origin = viewInverse * vec4(0,0,0,1);
    vec4 target = projInverse * vec4(d.x, d.y, 1, 1) ;
    vec4 direction = viewInverse*vec4(normalize(target.xyz), 0) ;

    hRec.isect.x = origin.xyz;
    hRec.wi = normalize(direction.xyz);
}


void reset(inout HitRecord hRec) {

}

void init(inout HitRecord hRec, vec2 id, vec2 screenSize, uint frame) {
    reset(hRec);
    hRec.isect.x = vec3(0);
    hRec.t = 0;
    hRec.rngState = initRNG(id, screenSize, frame);

    vec2 offset = hammersley(currentSample, sampleCount);
    generateRay(hRec, id, screenSize, offset);
}

Material defaultMaterial() {
    return Material(vec3(0.6), 0, 0.5, DIFFUSE_BRDF_LAMBERTIAN | SPECULAR_BRDF_MICROFACET);
}

MediumInteraction defaulMi() {
    return MediumInteraction(vec3(0), vec3(0), false);
}


#endif // VOLUME_PATH_TRACER_DOMAIN_GLSL