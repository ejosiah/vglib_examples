#version 460

#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_ray_tracing_position_fetch : enable

#include "path_tracing/eval_brdf.glsl"

#define BASE_COLOR_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_ROUGHNESS_INDEX 2
#define OCCLUSION_INDEX 3
#define EMISSION_INDEX 4
#define THICKNESS_INDEX 5
#define CLEAR_COAT_INDEX 6
#define CLEAR_COAT_ROUGHNESS_INDEX 7
#define CLEAR_COAT_NORMAL_INDEX 8
#define SHEEN_COLOR_INDEX 9
#define SHEEN_ROUGHNESS_INDEX 10
#define ANISOTROPY_INDEX 11
#define SPECULAR_STRENGTH_INDEX 12
#define SPECULAR_COLOR_INDEX 13
#define IRIDESCENCE_INDEX 14
#define IRIDESCENCE_THICKNESS_INDEX 15
#define TRANSMISSION_INDEX 16
#define TEXTURE_INFO_PER_MATERIAL 20

#define MATERIAL (instance.material)

#define TEXTURE_OFFSET (MATERIAL.textureInfoOffset * TEXTURE_INFO_PER_MATERIAL)

#define BASE_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + BASE_COLOR_INDEX]
#define NORMAL_TEX_INFO textureInfos[TEXTURE_OFFSET + NORMAL_INDEX]
#define METAL_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + METALLIC_ROUGHNESS_INDEX]
#define OCCLUSION_TEX_INFO textureInfos[TEXTURE_OFFSET + OCCLUSION_INDEX]


#define EMISSION_TEX_INFO textureInfos[TEXTURE_OFFSET + EMISSION_INDEX]
#define THICKNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + THICKNESS_INDEX]

#define CLEAR_COAT_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_INDEX]
#define CLEAR_COAT_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_ROUGHNESS_INDEX]
#define CLEAR_COAT_NORMAL_TEX_INFO textureInfos[TEXTURE_OFFSET + CLEAR_COAT_NORMAL_INDEX]

#define SHEEN_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + SHEEN_COLOR_INDEX]
#define SHEEN_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + SHEEN_ROUGHNESS_INDEX]

#define ANISOTROPY_TEX_INFO textureInfos[TEXTURE_OFFSET + ANISOTROPY_INDEX]

#define SPECULAR_STRENGTH_TEX_INFO textureInfos[TEXTURE_OFFSET + SPECULAR_STRENGTH_INDEX]
#define SPECULAR_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + SPECULAR_COLOR_INDEX]

#define IRIDESCENCE_TEX_INFO textureInfos[TEXTURE_OFFSET + IRIDESCENCE_INDEX]
#define IRIDESCENCE_THICKNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + IRIDESCENCE_THICKNESS_INDEX]

#define TRANSMISSION_TEX_INFO textureInfos[TEXTURE_OFFSET + TRANSMISSION_INDEX]

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_INFO.index)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_INFO.index)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_INFO.index)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_INFO.index)]

#define EMISSION_TEXTURE global_textures[nonuniformEXT(EMISSION_TEX_INFO.index)]
#define THICKNESS_TEXTURE global_textures[nonuniformEXT(THICKNESS_TEX_INFO.index)]

#define CLEAR_COAT_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_TEX_INFO.index)]
#define CLEAR_COAT_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_ROUGHNESS_TEX_INFO.index)]
#define CLEAR_COAT_NORMAL_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_NORMAL_TEX_INFO.index)]

#define SHEEN_COLOR_TEXTURE global_textures[nonuniformEXT(SHEEN_COLOR_TEX_INFO.index)]
#define SHEEN_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(SHEEN_ROUGHNESS_TEX_INFO.index)]

#define ANISOTROPY_TEXTURE global_textures[nonuniformEXT(ANISOTROPY_TEX_INFO.index)]

#define SPECULAR_STRENGTH_TEXTURE global_textures[nonuniformEXT(SPECULAR_STRENGTH_TEX_INFO.index)]
#define SPECULAR_COLOR_TEXTURE global_textures[nonuniformEXT(SPECULAR_COLOR_TEX_INFO.index)]

#define IRIDESCENCE_TEXTURE global_textures[nonuniformEXT(IRIDESCENCE_TEX_INFO.index)]
#define IRIDESCENCE_THICKNESS_TEXTURE global_textures[nonuniformEXT(IRIDESCENCE_THICKNESS_TEX_INFO.index)]

#define TRANSMISSION_TEXTURE global_textures[nonuniformEXT(TRANSMISSION_TEX_INFO.index)]

#define u_GGXLUT global_textures[nonuniformEXT(brdf_lut_texture_id)]
#define u_CharlieLUT global_textures[nonuniformEXT(charlie_lut_texture_id)]
#define u_SheenELUT global_textures[nonuniformEXT(sheen_lut_texture_id)]
#define u_GGXEnvSampler global_textures[nonuniformEXT(specular_texture_id)]
#define u_CharlieEnvSampler global_textures[nonuniformEXT(charlie_env_texture_id)]
#define u_LambertianEnvSampler global_textures[nonuniformEXT(irradiance_texture_id)]
#define u_TransmissionFramebufferSampler global_textures[nonuniformEXT(framebuffer_texture_id)]
#define MODEL_MATRIX (model * meshes[nonuniformEXT(drawId)].model)

#include "domain.glsl"

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;
InstanceData instance;

struct DebugData {
    vec3 origin;
    vec3 offsetOrigin;
    vec3 normal;
    vec3 rayDir;
    vec3 viewDir;
    int reflect;
};

layout(set = 3, binding = 0, scalar) buffer DEBUG_DATA_SSBO {
    DebugData rtx_debug[];
};

layout(set = 3, binding = 1, scalar) buffer DEBUG_REQUEST {
    uvec2 mouse_position;
    int on;
    int counter;
} debug_request;

layout(location = 0) rayPayloadIn HitRecord hRec;

hitAttribute vec2 bc;

float u = 1 - bc.x - bc.y;
float v = bc.x;
float w = bc.y;

Surface getSurfaceInfo();

void main() {
    load(instance, bc, gl_InstanceCustomIndex, gl_PrimitiveID);

    Surface surface = getSurfaceInfo();
    vec3 wo = -gl_WorldRayDirection;
    float Air = 1.0;

    if(MATERIAL.unlit == 1) {
        hRec.wi =  vec3(0);
        hRec.brdfWeigth = surface.albedo;
        return;
    }


    if(dot(surface.gN, wo) < 0){
        surface.inside = true;
        surface.gN *= -1;
        surface.sN *= -1;
    }

    vec3 origin, direction;
    vec3 brdfWeight = surface.albedo;
    if(MATERIAL.transmission > 0) {
        const float ior = surface.ior;
        vec3 F0 = vec3(pow((ior - 1.0) / (ior + 1.0), 2.0));
        float cosTheta = clamp(dot(wo, surface.sN), 0.0, 1.0);
        vec3 fresnel = evalFresnelSchlick(F0, 1.0, cosTheta);
        float randVal = rand(hRec.rngState);

        float reflectProb = clamp(dot(fresnel, vec3(0.333)), 0.0, 1.0);
        if (randVal < reflectProb) { // OR use average(luminance(fresnel))
            direction = reflect(-wo, surface.gN);
            origin = offsetRay(surface.x, surface.gN);
            brdfWeight *= fresnel / max(reflectProb, 0.001); // Avoid divide-by-zero
        } else {
            float eta = computeEta(MATERIAL, surface.inside);
            direction = refract(-wo, surface.gN, eta);
            origin = offsetRay(surface.x, -surface.gN);
            brdfWeight *= (vec3(1.0) - fresnel) / max(1.0 - reflectProb, 0.001);

            // Apply Beer's law (attenuation)
            float d = MATERIAL.attenuationDistance;
            float x = gl_HitT;
            vec3 C = MATERIAL.attenuationColor;
            if (surface.inside && d > 0) {
                hRec.transmission = pow(C, vec3(x / d));
            }
        }
        hRec.x = origin;
        hRec.wi =  direction;
        hRec.Le = surface.emission;
        hRec.brdfWeigth = brdfWeight;

        return;
    }

    surface.bsdf = SPECULAR_BRDF_MICROFACET | DIFFUSE_BRDF_LAMBERTIAN;

    origin = offsetRay(surface.x, surface.gN);
    brdfWeight = getBrdfWeight(surface, hRec.rngState, wo, direction);

    hRec.x = offsetRay(surface.x, surface.gN);
    hRec.wi =  direction;
    hRec.Le = surface.emission;
    hRec.brdfWeigth = brdfWeight;

}

#include "impl_pt.glsl"

Surface getSurfaceInfo() {
    NormalInfo ni =  getNormalInfo();
    const vec3 mro = getMRO();
    const vec4 baseColor = getBaseColor();
    Surface surface;
    surface.albedo = baseColor.rgb;
    surface.emission = getEmission();
    surface.x = instance.position.xyz;
    surface.gN = ni.Ng;
    surface.sN = ni.N;
    surface.metalness = clamp(mro.r, 0, 1);
    surface.roughness = clamp(mro.g, 0, 1);
    surface.transmission = vec3(1);
    surface.ior = instance.material.ior;
    surface.inside = false;

    if(instance.material.alphaMode == ALPHA_MODE_BLEND) {
        surface.albedo = mix(vec3(1), surface.albedo, baseColor.a);
        instance.material.transmission = 1;
    }

    return surface;
}
