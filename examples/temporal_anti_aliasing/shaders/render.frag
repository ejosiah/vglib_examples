#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define BASE_COLOR_INDEX 0
#define NORMAL_INDEX 1
#define METALLIC_ROUGHNESS_INDEX 2
#define OCCLUSION_INDEX 3

#define EMISSION_INDEX 0

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
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

#include "gltf.glsl"
#include "lighting.glsl"

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(set = 0, binding = 1) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 7) in flat int drawId;

float saturate(float x);
vec4 getBaseColor();
vec3 getNormal();
vec3 getMRO();

layout(location = 0) out vec4 fragColor;

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);

void main() {

    const vec4 baseColor = getBaseColor();

    if(MATERIAL.alphaMode == ALPHA_MODE_MASK && baseColor.a < MATERIAL.alphaCutOff)  {
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
    float specularWeight = 1;

    const vec3 L = normalize(vec3(1));
    const vec3 N = getNormal();
    const vec3 V = normalize(fs_in.eyes - fs_in.position);
    const vec3 H = normalize(L + V);

    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));
    const float LdotH = saturate(dot(L, H));
    const float VdotH = saturate(dot(V, H));

    vec3 radiance = vec3(0);
    if(NdotL > 0 || NdotV > 0){

        vec3 intensity = vec3(10);

        vec3 diffuse = vec3(0);
        vec3 specular = vec3(0);
        vec3 ambient = baseColor.rgb * 0.2;

        diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
        specular += intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);

        radiance = ambient * ao + diffuse + specular;
    }


    radiance /= 1 + radiance;
    radiance = pow(radiance, vec3(0.4545));

    fragColor = vec4(radiance, baseColor.a);

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
        mro.r = res.b;
        mro.g = res.g;

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
    if(NORMAL_TEX_ID == -1) {
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
