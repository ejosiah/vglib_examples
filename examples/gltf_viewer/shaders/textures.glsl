#ifndef TEXTURES_GLSL
#define TEXTURES_GLSL

#define BASE_COLOR_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_ROUGHNESS_INDEX 2
#define OCCLUSION_INDEX 3

#define EMISSION_INDEX 0

int meshId = -1;

#define MATERIAL_ID meshes[nonuniformEXT(meshId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define BASE_COLOR_TEX_ID MATERIAL.textures0[BASE_COLOR_INDEX]
#define METAL_ROUGHNESS_TEX_ID MATERIAL.textures0[METALLIC_ROUGHNESS_INDEX]
#define OCCLUSION_TEX_ID MATERIAL.textures0[OCCLUSION_INDEX]
#define NORMAL_TEX_ID MATERIAL.textures0[NORMAL_INDEX]

#define EMISSION_TEX_ID MATERIAL.textures1[EMISSION_INDEX]

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_ID)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_ID)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_ID)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_ID)]
#define EMISSION_TEXTURE global_textures[nonuniformEXT(EMISSION_TEX_ID)]

#define u_GGXLUT global_textures[brdf_lut_texture_id]
#define u_GGXEnvSampler global_textures[nonuniformEXT(specular_texture_id)]
#define u_LambertianEnvSampler global_textures[nonuniformEXT(irradiance_texture_id)]

layout(set = 2, binding = 10) uniform sampler2D global_textures[];

layout(push_constant) uniform Constants {
    layout(offset=192)
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
};

#endif // TEXTURES_GLSL