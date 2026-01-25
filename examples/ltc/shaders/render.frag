#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable

#include "ltc.glsl"

#define MATERIAL_SET 1
#define MATERIAL_BINDING_POINT 0
#define LIGHT_BINDING_POINT 1
#define LIGHT_INSTANCE_BINDING_POINT 2
#define TEXTURE_INFO_BINDING_POINT 3

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

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

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

#define LTC_MAG_TEXTURE global_textures[0]
#define LTC_MAT_TEXTURE global_textures[1]
#define LIGHT_SOURCE_PREFILTED_TEXTURE global_textures[3]

layout(set = 2, binding = 10) uniform sampler2D global_textures[];
layout(set = 2, binding = 10) uniform samplerCube global_textures_cubemap[];

layout(set = 3, binding = 0, scalar) uniform Options {
    mat4  transform;
    vec3 eyes;
    vec2 resolution;
    int   sampleCount;
    float roughness;
    vec3  dcolor;
    vec3  scolor;

    float intensity;
    float width;
    float height;
    float roty;
    float rotz;

    int twoSided;
};

layout(set = 3, binding = 1, scalar) uniform Light_Points {
    vec3 points[4];
} light;

#include "gltf.glsl"

layout(set = 0, binding = 0, scalar) buffer MeshData {
    Mesh meshes[];
};

layout(set = MATERIAL_SET, binding = MATERIAL_BINDING_POINT) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = MATERIAL_SET, binding = TEXTURE_INFO_BINDING_POINT) buffer TextureInfos {
    TextureInfo textureInfos[];
};

layout(location = 0) in struct {
    vec4 color;
    vec3 localPos;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv[2];
} fs_in;

layout(location = 13) in flat int drawId;

layout(location = 0) out vec4 fragColor;

const float gamma = 2.2;
const vec3 F0 = vec3(0.04);

float saturate(float v){ return clamp(v, 0.0, 1.0); }
vec3 PowVec3(vec3 v, float p){ return vec3(pow(v.x, p), pow(v.y, p), pow(v.z, p)); }
vec3 ToLinear(vec3 v) { return PowVec3(v, gamma); }
vec3 ToSRGB(vec3 v)   { return PowVec3(v, 1.0/gamma); }

void main() {
    vec4 baseColor = getBaseColor();

    if(MATERIAL.alphaMode == ALPHA_MODE_MASK)  {
        if(baseColor.a < MATERIAL.alphaCutOff){
            discard;
        }
        baseColor.a = 1;
    }

    vec3 mro = getMRO();
    const float metalness = clamp(mro.r, 0, 1);
    float roughness = clamp(mro.g, 0, 1);
    roughness = max(pow(roughness, 2), 0.03);
    vec3 lcol = vec3(intensity);
    vec3 dcol = baseColor.rgb * (1 - metalness);
    vec3 scol = mix(F0, baseColor.rgb, metalness);

    vec3 col = vec3(0);

    vec3 N = normalize(getNormalInfo().N);
    vec3 V = normalize(eyes - fs_in.position);

    vec3 points[4];
    points[0] = (transform * vec4(light.points[0], 1)).xyz;
    points[1] = (transform * vec4(light.points[1], 1)).xyz;
    points[2] = (transform * vec4(light.points[2], 1)).xyz;
    points[3] = (transform * vec4(light.points[3], 1)).xyz;

    vec3 pos = fs_in.position;

    float theta = acos(dot(N, V));
    vec2 uv = vec2(roughness, theta/(0.5*pi));
    uv = uv*LUT_SCALE + LUT_BIAS;

    vec4 t = texture(LTC_MAT_TEXTURE, uv);
    mat3 Minv = mat3(
    vec3(1, 0, t.y),
    vec3(0, t.z, 0),
    vec3(t.w, 0, t.x)
    );

    vec3 spec = LTC_Evaluate(N, V, pos, Minv, points, twoSided == 1, LIGHT_SOURCE_PREFILTED_TEXTURE);
    vec2 schlick = texture(LTC_MAG_TEXTURE, uv).rg;
    spec = spec * schlick.x + (1.0 - spec)*schlick.y;

    vec3 diff = LTC_Evaluate(N, V, pos, mat3(1), points, twoSided == 1, LIGHT_SOURCE_PREFILTED_TEXTURE);
    vec3 ambient = baseColor.rgb * 0.08 * mro.b;

    col  = lcol*(scol*spec + dcol*diff);
    col /= 2.0*pi;
    col += ambient;

    fragColor = vec4(col, baseColor.a);
    fragColor.rgb /= (fragColor.rgb + 1);


}

#include "gltf_impl.glsl"
