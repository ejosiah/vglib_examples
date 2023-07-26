#ifndef GLSL_EVAL_BRDF
#define GLSL_EVAL_BRDF

float getBrdfProbability(vec3 V, vec3 sN, vec3 albedo, float metalness) {

    // Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet unknown at this point
    vec3 specularF0 = vec3(luminance(baseColorToSpecularF0(albedo, metalness)));
    float diffuseReflectance = luminance(baseColorToDiffuseReflectance(albedo, metalness));
    float Fresnel = saturate(luminance(evalFresnel(specularF0, shadowedF90(specularF0), max(0.0f, dot(V, sN)))));

    // Approximate relative contribution of BRDFs using the Fresnel term
    float specular = Fresnel;
    float diffuse = diffuseReflectance * (1.0f - Fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
    float p = (specular / max(0.0001f, (specular + diffuse)));

    // Clamp probability to avoid undersampling of less prominent BRDF
    return clamp(p, 0.1f, 0.9f);
}

float getBrdfProbability(Surface surface, vec3 wo){
    return getBrdfProbability(wo, surface.sN, surface.albedo, surface.metalness);
}

vec3 evalIndirectBrdf(Surface surface, int brdfType, vec3 wo, inout RngStateType rngState, out vec3 wi){

    BrdfArgs args;
    args.wo = wo;
    args.surfacePoint = surface.x;
    args.surfaceNormal = surface.sN;
    args.surfaceGeomNormal = surface.gN;
    args.surfaceAlbedo = surface.albedo;
    args.surfaceMetalness = surface.metalness;
    args.surfaceRoughness = surface.roughness;
    args.rngState = rngState;
    args.brdfType = brdfType;

    vec3 sampleWeight = evalIndirectCombinedBRDF(args);

    rngState = args.rngState;
    wi = args.wi;
    return sampleWeight;
}

vec3 evalBrdf(Surface surface, vec3 wo, vec3 wi){
    if(surface.volume){
        return surface.albedo;
    }
    BrdfArgs args;
    args.wo = wo;
    args.wi = wi;
    args.surfacePoint = surface.x;
    args.surfaceNormal = surface.sN;
    args.surfaceGeomNormal = surface.gN;
    args.surfaceAlbedo = surface.albedo;
    args.surfaceMetalness = surface.metalness;
    args.surfaceRoughness = surface.roughness;

    return evalCombinedBRDF(args);
}

vec3 getBrdfWeight(Surface surface, inout RngStateType rngState, vec3 wo, out vec3 wi){
    int brdfType;
    vec3 weight = vec3(1);
    if(isMirror(surface)){
        brdfType = BRDF_SPECULAR;
    }else {
        float brdfProbability = getBrdfProbability(surface, wo);
        if(rand(rngState) < brdfProbability){
            brdfType = BRDF_SPECULAR;
            weight /= brdfProbability;
        }else{
            brdfType = BRDF_DIFFUSE;
            weight /= (1 - brdfProbability);
        }
    }

    weight *= evalIndirectBrdf(surface, brdfType, wo, rngState, wi);

    return weight;
}
#endif // GLSL_EVAL_BRDF