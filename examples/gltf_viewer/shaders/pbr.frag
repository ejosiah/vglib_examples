#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define BASE_COLOR_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_ROUGHNESS_INDEX 2
#define OCCLUSION_INDEX 3

#define EMISSION_INDEX 0
#define THICKNESS_INDEX 1

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define BASE_COLOR_TEX_ID MATERIAL.textures0[BASE_COLOR_INDEX]
#define METAL_ROUGHNESS_TEX_ID MATERIAL.textures0[METALLIC_ROUGHNESS_INDEX]
#define OCCLUSION_TEX_ID MATERIAL.textures0[OCCLUSION_INDEX]
#define NORMAL_TEX_ID MATERIAL.textures0[NORMAL_INDEX]

#define EMISSION_TEX_ID MATERIAL.textures1[EMISSION_INDEX]
#define THICKNESS_TEX_ID MATERIAL.textures1[THICKNESS_INDEX]

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_ID)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_ID)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_ID)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_ID)]
#define EMISSION_TEXTURE global_textures[nonuniformEXT(EMISSION_TEX_ID)]
#define THICKNESS_TEXTURE global_textures[nonuniformEXT(THICKNESS_TEX_ID)]

#define u_GGXLUT global_textures[brdf_lut_texture_id]
#define u_GGXEnvSampler global_textures[nonuniformEXT(specular_texture_id)]
#define u_LambertianEnvSampler global_textures[nonuniformEXT(irradiance_texture_id)]
#define u_TransmissionFramebufferSampler global_textures[nonuniformEXT(framebuffer_texture_id)]
#define MODEL_MATRIX (model * meshes[nonuniformEXT(drawId)].model)

layout(set = 2, binding = 10) uniform sampler2D global_textures[];


layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
    int framebuffer_texture_id;
    int discard_transmissive;
};

#include "gltf.glsl"
#include "octahedral.glsl"
#include "ibl.glsl"

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(set = 1, binding = 0) buffer GLTF_MATERIAL {
    Material materials[];
};


layout(location = 0) in struct {
    mat4 localToWorld;
    vec3 localPos;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 12) in flat int drawId;

float saturate(float x);
vec4 getBaseColor();
vec3 getNormal();
vec3 getMRO();
bool noTangets();
vec3 getEmission();
float getTransmissionFactor();
float getThickness();

layout(location = 0) out vec4 fragColor;

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;

void main() {


    vec4 baseColor = getBaseColor();

    if(MATERIAL.alphaMode == ALPHA_MODE_MASK)  {
        if(baseColor.a < MATERIAL.alphaCutOff){
            discard;
        }
        baseColor.a = 1;
    }

    const float transmissionFactor = getTransmissionFactor();

    if((MATERIAL.alphaMode == ALPHA_MODE_BLEND || transmissionFactor > 0) && discard_transmissive == 1){
        discard;
    }

    const vec3 mro = getMRO();
    const float metalness = mro.r;
    const float roughness = mro.g;
    const float ao = mro.b;
    const float alphaRoughness = roughness * roughness;
    const vec3 f0 = mix(F0, baseColor.rgb, metalness);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float ior = IOR;
    const float specularWeight = 1;
    const float thickness = getThickness();
    vec3 attenuationColor = MATERIAL.attenuationColor;
    float attenuationDistance = MATERIAL.attenuationDistance;

    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);
    vec3 f_emissive = vec3(0.0);
    vec3 f_clearcoat = vec3(0.0);
    vec3 f_sheen = vec3(0.0);
    vec3 f_transmission = vec3(0.0);
    float albedoSheenScaling = 1.0;

    float dispersion = 0;

    vec3 N = getNormal();
    vec3 V = normalize(fs_in.eyes - fs_in.position);
    f_emissive = getEmission();


    f_specular += getIBLRadianceGGX(N, V, roughness, f0, specularWeight);
    f_diffuse += getIBLRadianceLambertian(N, V, roughness, c_diff, f0, specularWeight);

    f_transmission += getIBLVolumeRefraction(N, V, roughness, c_diff, f0, f90, fs_in.position, MODEL_MATRIX, view, projection
                                             ,ior, thickness, attenuationColor, attenuationDistance, dispersion);

    vec3 f_diffuse_ibl = f_diffuse;
    vec3 f_specular_ibl = f_specular;

    vec3 diffuse;
    vec3 specular;

    diffuse = f_diffuse + mix(f_diffuse_ibl, f_diffuse_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;
    // apply ambient occlusion to all lighting that is not punctual
    specular = f_specular + mix(f_specular_ibl, f_specular_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;

    diffuse = mix(diffuse, f_transmission, transmissionFactor);

    vec3 color = vec3(0);
    color = f_emissive + diffuse + specular;

    color /= 1 + color;
    color = pow(color, vec3(0.4545));
    fragColor = vec4(color, baseColor.a);
}

float saturate(float x) {
    return clamp(x, 0, 1);
}

vec4 getBaseColor() {
    if(BASE_COLOR_TEX_ID == -1){
        return vec4(MATERIAL.baseColor);
    }
    vec4 color = texture(BASE_COLOR_TEXTURE, fs_in.uv);
    color.rgb = pow(color.rgb, vec3(2.2));
    return color;
}

vec3 getMRO() {
    vec3 mro;
    mro.r = MATERIAL.metalness;
    mro.g = MATERIAL.roughness;
    mro.b = 1;

    if(METAL_ROUGHNESS_TEX_ID != -1) {
        vec3 res = texture(METAL_ROUGHNESS_TEXTURE, fs_in.uv).rgb;
        mro.r *= res.b;
        mro.g *= res.g;

        if(OCCLUSION_TEX_ID != 1) {
            if(OCCLUSION_TEX_ID == METAL_ROUGHNESS_TEX_ID) {
                mro.b = res.r;
            }else {
                mro.b = texture(OCCLUSION_TEXTURE, fs_in.uv).r;
            }
        }
    }
    return mro;
}

vec3 getNormal() {
    if(NORMAL_TEX_ID == -1 || noTangets()) {
        return fs_in.normal;
    }
    mat3 TBN = mat3(fs_in.tangent, fs_in.bitangent, fs_in.normal);
    vec3 normal = 2 * texture(NORMAL_TEXTURE, fs_in.uv).xyz - 1;
    normal.y *= -1;
    normal = normalize(TBN * normal);

    if(MATERIAL.doubleSided == 1 && !gl_FrontFacing) {
        normal *= -1;
    }
    return normal;
}

bool noTangets() {
    return all(equal(fs_in.tangent, vec3(0)));
}

vec3 getEmission(){
    vec3 emission = MATERIAL.emission;
    if(EMISSION_TEX_ID != -1) {
        emission *= texture(EMISSION_TEXTURE, fs_in.uv).rgb;
    }
    return emission;
}

float getTransmissionFactor() {
    return MATERIAL.transmission;
}

float getThickness() {
    float thickness = MATERIAL.thickness;
    if(THICKNESS_TEX_ID != -1){
        thickness *= texture(THICKNESS_TEXTURE, fs_in.uv).r;
    }
    return thickness;
}