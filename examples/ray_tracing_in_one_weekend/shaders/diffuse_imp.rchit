#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_EXT_ray_tracing_position_fetch : enable
#extension GL_EXT_buffer_reference2 : require

#include "ray_tracing_lang.glsl"
#include "common.glsl"
#include "perlin_noise.glsl"


layout(buffer_reference, buffer_reference_align=8) buffer SphereBuffer {
    Sphere at[];
};

layout(buffer_reference, buffer_reference_align=8) buffer MaterialBuffer {
    Diffuse at[];
};

layout(shaderRecord, std430) buffer SBT {
    SphereBuffer spheres;
    MaterialBuffer materials;
};


layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

float turb(vec3 p) {
    return abs(perlin_fbm(p, 2.0, 7));
}

void main() {

    Sphere sphere = spheres.at[gl_PrimitiveID];

    vec3 p, n;
    getSurfaceInfo(sphere, gl_WorldRayOrigin, gl_WorldRayDirection, gl_HitT, p, n);

    hRec.n = n;
    hRec.x = p;
    vec3 wi = cosineSampleHemisphere(sampleVec2(hRec));

    vec3 tn, bn;
    othonormalBasis(tn, bn, n);
    mat3 TBN = mat3(tn, bn, n);
    hRec.wi = TBN * wi;

    Diffuse material = materials.at[gl_PrimitiveID];
    vec3 attenuation = material.albedo;
    if(material.textureId != -1) {
        if(material.textureType == 0){
            if (material.useTriplanarMapping == 1){
                attenuation = triplanerSample(global_textures[material.textureId], p, n, material.scale).rgb;
            } else {
                attenuation = texture(global_textures[material.textureId], bc * material.scale).rgb;
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