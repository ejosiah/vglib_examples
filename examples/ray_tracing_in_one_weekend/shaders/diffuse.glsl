#ifndef DIFFUSE_GLSL
#define DIFFUSE_GLSL

#include "common.glsl"
#include "perlin_noise.glsl"

struct Diffuse {
    vec3 albedo;
    int textureId;

    vec3 emission;
    int textureType;

    float scale;
    int useTriplanarMapping;
    int padding[2];
};

float turb(vec3 p) {
    return abs(perlin_fbm(p, 2.0, 7));
}

void compute_diffuse_bsdf(vec3 p, vec3 n, vec2 uv, Diffuse material, inout HitRecord hRec) {
    vec3 wi = cosineSampleHemisphere(sampleVec2(hRec));

    vec3 tn, bn;
    othonormalBasis(tn, bn, n);
    mat3 TBN = mat3(tn, bn, n);
    hRec.wi = TBN * wi;
    hRec.n = n;
    hRec.x = p;

    vec3 attenuation = material.albedo;
    if(material.textureId != -1) {
        if(material.textureType == 0){
            if (material.useTriplanarMapping == 1){
                attenuation = triplanerSample(global_textures[material.textureId], p, n, material.scale).rgb;
            } else {
                attenuation = texture(global_textures[material.textureId], uv * material.scale).rgb;
            }
        }else {
            float s = material.scale;
            float c = (1 + sin(s * p.y + 10 * turb(p))) * 0.5;
            attenuation = vec3(c);
        }
    }
    hRec.attenuation = attenuation;
    hRec.emission = material.emission;
}

#endif // DIFFUSE_GLSL