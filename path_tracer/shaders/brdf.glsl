#ifndef BRDF_GLSL
#define BRDF_GLSL

#include "fresnel.glsl"
#include "common.glsl"
#include "quaternion.glsl"

BrdfData prepareBRDFData(BrdfArgs args){
    vec3 N = args.surfaceNormal;
    vec3 V = normalize(args.wo);
    vec3 L = args.wi;

    BrdfData data;
    data.L = L;
    data.V = V;
    data.sN = N;
    data.H = normalize(L + V);

    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    data.Vbackfacing = (NdotV <= 0.0f);
    data.Lbackfacing = (NdotL <= 0.0f);

    data.NdotL = clamp(NdotL, 0.00001, 1);
    data.NdotV = clamp(NdotV, 0.00001, 1);

    data.LdotH = clamp(dot(L, data.H), 0, 1);
    data.NdotH = clamp(dot(N, data.H), 0, 1);
    data.VdotH = clamp(dot(V, data.H), 0, 1);

    data.specularF0 = baseColorToSpecularF0(args.surfaceAlbedo, args.surfaceMetalness);
    data.diffuseReflectance = baseColorToDiffuseReflectance(args.surfaceAlbedo, args.surfaceMetalness);

    // Unpack 'perceptively linear' -> 'linear' -> 'squared' roughness
    data.roughness = args.surfaceRoughness;
    data.alpha = args.surfaceRoughness * args.surfaceRoughness;
    data.alphaSquared = data.alpha * data.alpha;

    // Pre-calculate some more BRDF terms
    data.F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.LdotH);

    return data;
}

float beckmannAlphaToOrenNayarRoughness(float alpha) {
    return ONE_VER_SQRT_TWO * atan(alpha);
}

float orenNayar(BrdfData data) {

    // Oren-Nayar roughness (sigma) is in radians - use conversion from Beckmann roughness here
    float sigma = beckmannAlphaToOrenNayarRoughness(data.alpha);

    float thetaV = acos(data.NdotV);
    float thetaL = acos(data.NdotL);

    float alpha = max(thetaV, thetaL);
    float beta = min(thetaV, thetaL);

    // Calculate cosine of azimuth angles difference - by projecting L and V onto plane defined by N. Assume L, V, N are normalized.
    vec3 l = data.L - data.NdotL * data.sN;
    vec3 v = data.V - data.NdotV * data.sN;
    float cosPhiDifference = dot(normalize(v), normalize(l));

    float sigma2 = sigma * sigma;
    float A = 1.0f - 0.5f * (sigma2 / (sigma2 + 0.33f));
    float B = 0.45f * (sigma2 / (sigma2 + 0.09f));

    return (A + B * max(0.0f, cosPhiDifference) * sin(alpha) * tan(beta));
}

float disneyDiffuse(const BrdfData data) {

    float FD90MinusOne = 2.0f * data.roughness * data.LdotH * data.LdotH - 0.5f;

    float FDL = 1.0f + (FD90MinusOne * pow(1.0f - data.NdotL, 5.0f));
    float FDV = 1.0F + (FD90MinusOne * pow(1.0f - data.NdotV, 5.0f));

    return FDL * FDV;
}


vec3 evalVoid(BrdfData data){
    return vec3(0);
}

vec3 evalLamberian(BrdfData data){
    return data.diffuseReflectance * (ONE_OVER_PI * data.NdotL);
}

vec3 evalOrenNayar(BrdfData data){
    return data.diffuseReflectance * (orenNayar(data) * ONE_OVER_PI * data.NdotL);
}

vec3 evalDisneyDiffuse(const BrdfData data) {
    return data.diffuseReflectance * (disneyDiffuse(data) * ONE_OVER_PI * data.NdotL);
}


vec3 evalDiffuse(BrdfData data){
    switch(diffuse_brdf_type){
        case DIFFUSE_BRDF_LAMBERTIAN:
            return evalLamberian(data);
        case DIFFUSE_BRDF_OREN_NAYAR:
            return evalOrenNayar(data);
        case DIFFUSE_BRDF_DISNEY:
            return evalDisneyDiffuse(data);
        default:
            return evalVoid(data);
    }
}

float none(BrdfData data){
    return 0;
}

float lamberian(BrdfData data){
    return 1;
}

float diffuseTerm(BrdfData data, int type){
    switch(type){
        case DIFFUSE_BRDF_LAMBERTIAN:
            return lamberian(data);
        case DIFFUSE_BRDF_OREN_NAYAR:
            return orenNayar(data);
        case DIFFUSE_BRDF_DISNEY:
            return disneyDiffuse(data);
        default:
            return none(data);
    }
}

float Beckmann_D(float alphaSquared, float NdotH)
{
    float cos2Theta = NdotH * NdotH;
    float numerator = exp((cos2Theta - 1.0f) / (alphaSquared * cos2Theta));
    float denominator = PI * alphaSquared * cos2Theta * cos2Theta;
    return numerator / denominator;
}

float GGX_D(float alphaSquared, float NdotH) {
    float b = ((alphaSquared - 1.0f) * NdotH * NdotH + 1.0f);
    return alphaSquared / (PI * b * b);
}

float NDF(float alphaSquared, float NdotH){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return GGX_D(alphaSquared, NdotH);
        case NDF_FUNC_BECKMANN:
            return Beckmann_D(alphaSquared, NdotH);
        default:
            return 0;
    }
}

// Function to calculate 'a' parameter for lambda functions needed in Smith G term
// This is a version for shape invariant (isotropic) NDFs
// Note: makse sure NdotS is not negative
float Smith_G_a(float alpha, float NdotS) {
    return NdotS / (max(0.00001f, alpha) * sqrt(1.0f - min(0.99999f, NdotS * NdotS)));
}


// Smith G1 term (masking function) optimized for GGX distribution (by substituting G_Lambda_GGX into G1)
float Smith_G1_GGX(float a) {
    float a2 = a * a;
    return 2.0f / (sqrt((a2 + 1.0f) / a2) + 1.0f);
}

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared) {
    return 2.0f / (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}



// Smith G1 term (masking function) optimized for Beckmann distribution (by substituting G_Lambda_Beckmann_Walter into G1)
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
float Smith_G1_Beckmann_Walter(float a) {
    if (a < 1.6f) {
        return ((3.535f + 2.181f * a) * a) / (1.0f + (2.276f + 2.577f * a) * a);
    } else {
        return 1.0f;
    }
}

float Smith_G1_Beckmann_Walter(float alpha, float NdotS, float alphaSquared, float NdotSSquared) {
    return Smith_G1_Beckmann_Walter(Smith_G_a(alpha, NdotS));
}


float Smith_G1(float a){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return Smith_G1_GGX(a);
        case NDF_FUNC_BECKMANN:
            return Smith_G1_Beckmann_Walter(a);
        default:
            return 0;
    }
}

float Smith_G1(float alpha, float NdotS, float alphaSquared, float NdotSSquared){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return Smith_G1_GGX(alpha, NdotS, alphaSquared, NdotSSquared);
        case NDF_FUNC_BECKMANN:
            return Smith_G1_Beckmann_Walter(alpha, NdotS, alphaSquared, NdotSSquared);
        default:
            return 0;
    }
}

// Lambda function for Smith G term derived for GGX distribution
float Smith_G_Lambda_GGX(float a) {
    return (-1.0f + sqrt(1.0f + (1.0f / (a * a)))) * 0.5f;
}

// Lambda function for Smith G term derived for Beckmann distribution
// This is Walter's rational approximation (avoids evaluating of error function)
// Source: "Real-time Rendering", 4th edition, p.339 by Akenine-Moller et al.
// Note that this formulation is slightly optimized and different from Walter's
float Smith_G_Lambda_Beckmann_Walter(float a) {
    if (a < 1.6f) {
        return (1.0f - (1.259f - 0.396f * a) * a) / ((3.535f + 2.181f * a) * a);
        //return ((1.0f + (2.276f + 2.577f * a) * a) / ((3.535f + 2.181f * a) * a)) - 1.0f; //< Walter's original
    } else {
        return 0.0f;
    }
}

float Smith_G_Lambda(float a){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return Smith_G_Lambda_GGX(a);
        case NDF_FUNC_BECKMANN:
            return Smith_G_Lambda_Beckmann_Walter(a);
        default:
            return 0;
    }
}

// Smith G2 term (masking-shadowing function)
// Separable version assuming independent (uncorrelated) masking and shadowing, uses G1 functions for selected NDF
float Smith_G2_Separable(float alpha, float NdotL, float NdotV) {
    float aL = Smith_G_a(alpha, NdotL);
    float aV = Smith_G_a(alpha, NdotV);
    return Smith_G1(aL) * Smith_G1(aV);
}

// Smith G2 term (masking-shadowing function)
// Height correlated version - non-optimized, uses G_Lambda functions for selected NDF
float Smith_G2_Height_Correlated(float alpha, float NdotL, float NdotV) {
    float aL = Smith_G_a(alpha, NdotL);
    float aV = Smith_G_a(alpha, NdotV);
    return 1.0f / (1.0f + Smith_G_Lambda(aL) + Smith_G_Lambda(aV));
}


// Smith G2 term (masking-shadowing function) for GGX distribution
// Separable version assuming independent (uncorrelated) masking and shadowing - optimized by substituing G_Lambda for G_Lambda_GGX and
// dividing by (4 * NdotL * NdotV) to cancel out these terms in specular BRDF denominator
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Separable_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) {
    float a = NdotV + sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
    float b = NdotL + sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
    return 1.0f / (a * b);
}


// Smith G2 term (masking-shadowing function) for GGX distribution
// Height correlated version - optimized by substituing G_Lambda for G_Lambda_GGX and dividing by (4 * NdotL * NdotV) to cancel out
// the terms in specular BRDF denominator
// Source: "Moving Frostbite to Physically Based Rendering" by Lagarde & de Rousiers
// Note that returned value is G2 / (4 * NdotL * NdotV) and therefore includes division by specular BRDF denominator
float Smith_G2_Height_Correlated_GGX_Lagarde(float alphaSquared, float NdotL, float NdotV) {
    float a = NdotV * sqrt(alphaSquared + NdotL * (NdotL - alphaSquared * NdotL));
    float b = NdotL * sqrt(alphaSquared + NdotV * (NdotV - alphaSquared * NdotV));
    return 0.5f / (a + b);
}

// A fraction G2/G1 where G2 is height correlated can be expressed using only G1 terms
// Source: "Implementing a Simple Anisotropic Rough Diffuse Material with Stochastic Evaluation", Appendix A by Heitz & Dupuy
float Smith_G2_Over_G1_Height_Correlated(float alpha, float alphaSquared, float NdotL, float NdotV) {
    float G1V = Smith_G1(alpha, NdotV, alphaSquared, NdotV * NdotV);
    float G1L = Smith_G1(alpha, NdotL, alphaSquared, NdotL * NdotL);
    return G1L / (G1V + G1L - G1V * G1L);
}

float Smith_G2(float alpha, float alphaSquared, float NdotL, float NdotV) {
    if(bool(use_optimized_g2) && ndf_function == NDF_FUNC_GGX){
        if(bool(use_height_correlated_g2)){
            return Smith_G2_Height_Correlated_GGX_Lagarde(alphaSquared, NdotL, NdotV);
        }else{
            return Smith_G2_Separable_GGX_Lagarde(alphaSquared, NdotL, NdotV);
        }
    }else if(bool(use_height_correlated_g2)){
        return Smith_G2_Height_Correlated(alpha, NdotL, NdotV);
    }else {
        return Smith_G2_Separable(alpha, NdotL, NdotV);
    }

}


vec3 evalMicrofacet(BrdfData data){
    float D = NDF(max(0.00001f, data.alphaSquared), data.NdotH);
    float G2 = Smith_G2(data.alpha, data.alphaSquared, data.NdotL, data.NdotV);
    //vec3 F = evalFresnel(data.specularF0, shadowedF90(data.specularF0), data.VdotH); //< Unused, F is precomputed already

    if(bool(g2_divide_by_denomiator)) return data.F * (G2 * D * data.NdotL);
    else return ((data.F * G2 * D) / (4.0f * data.NdotL * data.NdotV)) * data.NdotL;
}

vec3 evalSpecular(BrdfData data){
    switch(specular_brdf_type){
        case SPECULAR_BRDF_MICROFACET:
            return evalMicrofacet(data);
        case SPECLUAR_BRDF_PHONG:
            return vec3(0, 1, 0);
        default:
            return evalVoid(data);
    }
}


vec3 evalCombinedBRDF(BrdfArgs args) {

    BrdfData data = prepareBRDFData(args);

    // Ignore V and L rays "below" the hemisphere
    if (data.Vbackfacing || data.Lbackfacing) return vec3(0.0f, 0.0f, 0.0f);

    // Eval specular and diffuse BRDFs
    vec3 specular = evalSpecular(data);
    vec3 diffuse = evalDiffuse(data);

    if(bool(combine_brdf_with_fresnel)){
        return (vec3(1.0f) - data.F) * diffuse + specular;
    }
    return diffuse + specular;

}

// Samples a microfacet normal for the GGX distribution using VNDF method.
// Source: "Sampling the GGX Distribution of Visible Normals" by Heitz
// See also https://hal.inria.fr/hal-00996995v1/document and http://jcgt.org/published/0007/04/01/
// Random variables 'u' must be in <0;1) interval
// PDF is 'G1(NdotV) * D'
vec3 sampleGGXVNDF(vec3 Ve, vec2 alpha2D, vec2 u) {

    // Section 3.2: transforming the view direction to the hemisphere configuration
    vec3 Vh = normalize(vec3(alpha2D.x * Ve.x, alpha2D.y * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0.0f ? vec3(-Vh.y, Vh.x, 0.0f) * rsqrt(lensq) : vec3(1.0f, 0.0f, 0.0f);
    vec3 T2 = cross(Vh, T1);

    // Section 4.2: parameterization of the projected area
    float r = sqrt(u.x);
    float phi = TWO_PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5f * (1.0f + Vh.z);
    t2 = mix(sqrt(1.0f - t1 * t1), t2, s);

    // Section 4.3: reprojection onto hemisphere
    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4: transforming the normal back to the ellipsoid configuration
    return normalize(vec3(alpha2D.x * Nh.x, alpha2D.y * Nh.y, max(0.0f, Nh.z)));
}

// Samples a microfacet normal for the Beckmann distribution using walter's method.
// Source: "Microfacet Models for Refraction through Rough Surfaces" by Walter et al.
// PDF is 'D * NdotH'
vec3 sampleBeckmannWalter(vec3 Vlocal, vec2 alpha2D, vec2 u) {
    float alpha = dot(alpha2D, vec2(0.5f, 0.5f));

    // Equations (28) and (29) from Walter's paper for Beckmann distribution
    float tanThetaSquared = -(alpha * alpha) * log(1.0f - u.x);
    float phi = TWO_PI * u.y;

    // Calculate cosTheta and sinTheta needed for conversion to H vector
    float cosTheta = rsqrt(1.0f + tanThetaSquared);
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);

    // Convert sampled spherical coordinates to H vector
    return normalize(vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta));
}


vec3 sampleSpecularHalfVector(vec3 Ve, vec2 alpha2D, vec2 u){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return sampleGGXVNDF(Ve, alpha2D, u);
        case NDF_FUNC_BECKMANN:
            return sampleBeckmannWalter(Ve, alpha2D, u);
        default:
            return vec3(0);
    }
}

// Weight for the reflection ray sampled from GGX distribution using VNDF method
float specularSampleWeightGGXVNDF(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) {
    if(bool(use_height_correlated_g2))
        return Smith_G2_Over_G1_Height_Correlated(alpha, alphaSquared, NdotL, NdotV);
    else
        return Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
}

float specularSampleWeightBeckmannWalter(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH) {
    return (HdotL * Smith_G2(alpha, alphaSquared, NdotL, NdotV)) / (NdotV * NdotH);
}


float specularSampleWeight(float alpha, float alphaSquared, float NdotL, float NdotV, float HdotL, float NdotH){
    switch(ndf_function){
        case NDF_FUNC_GGX:
            return specularSampleWeightGGXVNDF(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);
        case NDF_FUNC_BECKMANN:
            return specularSampleWeightBeckmannWalter(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);
        default:
            return 0;
    }
}

// Samples a reflection ray from the rough surface using selected microfacet distribution and sampling method
// Resulting weight includes multiplication by cosine (NdotL) term
vec3 sampleSpecularMicrofacet(vec3 Vlocal, float alpha, float alphaSquared, vec3 specularF0, vec2 u,out vec3 weight) {
    // Sample a microfacet normal (H) in local space
    vec3 Hlocal;
    if (alpha == 0.0f) {
        // Fast path for zero roughness (perfect reflection), also prevents NaNs appearing due to divisions by zeroes
        Hlocal = vec3(0.0f, 0.0f, 1.0f);
    } else {
        // For non-zero roughness, this calls VNDF sampling for GG-X distribution or Walter's sampling for Beckmann distribution
        Hlocal = sampleSpecularHalfVector(Vlocal, vec2(alpha, alpha), u);
    }

    // Reflect view direction to obtain light vector
    vec3 Llocal = reflect(-Vlocal, Hlocal);

    // Note: HdotL is same as HdotV here
    // Clamp dot products here to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
    float HdotL = max(0.00001f, min(1.0f, dot(Hlocal, Llocal)));
    const vec3 Nlocal = vec3(0.0f, 0.0f, 1.0f);
    float NdotL = max(0.00001f, min(1.0f, dot(Nlocal, Llocal)));
    float NdotV = max(0.00001f, min(1.0f, dot(Nlocal, Vlocal)));
    float NdotH = max(0.00001f, min(1.0f, dot(Nlocal, Hlocal)));
    vec3 F = evalFresnel(specularF0, shadowedF90(specularF0), HdotL);

    // Calculate weight of the sample specific for selected sampling method
    // (this is microfacet BRDF divided by PDF of sampling method - notice how most terms cancel out)
    weight = F * specularSampleWeight(alpha, alphaSquared, NdotL, NdotV, HdotL, NdotH);

    return Llocal;
}

vec3 sampleSpecular(vec3 Vlocal, float alpha, float alphaSquared, vec3 specularF0, vec2 u,out vec3 weight){
    switch(specular_brdf_type){
        case SPECULAR_BRDF_MICROFACET:
            return sampleSpecularMicrofacet(Vlocal, alpha, alphaSquared, specularF0, u, weight);
        case SPECLUAR_BRDF_PHONG:
            return vec3(0);
        default:
            return vec3(0);
    }
}

vec3 evalIndirectCombinedBRDF(inout BrdfArgs args){

    if(dot(args.surfaceNormal, args.wo) <= 0){
        return vec3(0);
    }

    BrdfArgs localArgs = args;

    vec3 N = args.surfaceNormal;
    vec3 T, B;
    othonormalBasis(T, B, N);
    mat3 localToWorld = mat3(T, B, N);
    mat3 worldToLocal = transpose(localToWorld);
    // transform wo from world space to local space
//    localArgs.wo = T * args.wo + B * args.wo + N * args.wo;
    localArgs.wo = worldToLocal * args.wo;
    localArgs.surfaceNormal = vec3(0, 0, 1);
    localArgs.wi = vec3(0);

    vec3 sampleWeight = vec3(1);

    if(args.brdfType == BRDF_DIFFUSE){
        vec2 u = randomVec2(args.rngState);
        localArgs.wi = sampleHemisphere(u);

        BrdfData data = prepareBRDFData(localArgs);

        sampleWeight = data.diffuseReflectance * diffuseTerm(data, args.diffuseType);

        if(bool(combine_brdf_with_fresnel)){
            // Sample a half-vector of specular BRDF
            u = randomVec2(args.rngState);
            vec3 Hspecular = sampleSpecularHalfVector(localArgs.wo, vec2(data.alpha, data.alpha), u);

            // Clamp HdotL to small value to prevent numerical instability. Assume that rays incident from below the hemisphere have been filtered
            float VdotH = max(0.00001f, min(1.0f, dot(localArgs.wo, Hspecular)));
            sampleWeight *= (vec3(1.0f) - evalFresnel(data.specularF0, shadowedF90(data.specularF0), VdotH));
        }

    }else if(args.brdfType == BRDF_SPECULAR){
        localArgs.wi = vec3(0, 0, 1);   // not used for specular
        BrdfData data = prepareBRDFData(localArgs);
        vec2 u = randomVec2(args.rngState);
        localArgs.wi = sampleSpecular(localArgs.wo, data.alpha, data.alphaSquared, data.specularF0, u, sampleWeight);
    }


    if(luminance(sampleWeight) == 0) return vec3(0);

    // transform wi from local space to world space;
//    args.wi = T * localArgs.wi.x + B * localArgs.wi.y + N * localArgs.wi.z;
    args.wi = localToWorld * localArgs.wi;
    if(dot(args.surfaceGeomNormal, args.wi) <= 0) return vec3(0);

    return sampleWeight;
}

#endif // BRDF_GLSL