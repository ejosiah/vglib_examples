#ifndef PATH_TRACING_DOMAIN_GLSL
#define PATH_TRACING_DOMAIN_GLSL

#include "random.glsl"

#define MIN_DIELECTRICS_F0 0.04f

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
    int specularType;
    int diffuseType;
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
    int specularType;
    int diffuseType;
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
    int specularType;
    int diffuseType;
};

bool isMirror(Surface surface){
    return surface.metalness == 1 && surface.roughness == 0;
}

#endif // PATH_TRACING_DOMAIN_GLSL