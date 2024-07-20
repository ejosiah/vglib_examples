#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

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
#define TEXTURE_INFO_PER_MATERIAL 12

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

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_INFO.index)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_INFO.index)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_INFO.index)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_INFO.index)]

#define EMISSION_TEXTURE global_textures[nonuniformEXT(EMISSION_TEX_INFO.index)]
#define THICKNESS_TEXTURE global_textures[nonuniformEXT(THICKNESS_TEX_INFO.index)]

#define CLEAR_COAT_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_TEX_INFO.index)]
#define CLEAR_COAT_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_ROUGHNESS_TEX_INFO.index)]
#define CLEAR_COAT_NORMAL_TEXTURE global_textures[nonuniformEXT(CLEAR_COAT_NORMAL_TEX_INFO.index)]

#define u_GGXLUT global_textures[brdf_lut_texture_id]
#define u_GGXEnvSampler global_textures[nonuniformEXT(specular_texture_id)]
#define u_LambertianEnvSampler global_textures[nonuniformEXT(irradiance_texture_id)]
#define u_TransmissionFramebufferSampler global_textures[nonuniformEXT(framebuffer_texture_id)]
#define MODEL_MATRIX (model * meshes[nonuniformEXT(drawId)].model)

#define UNIFORMS_SET 3
#define UNIFORMS_BINDING_POINT 0

layout(set = 2, binding = 10) uniform sampler2D global_textures[];

#include "uniforms.glsl"

#include "punctual_lights.glsl"
#include "gltf.glsl"
#include "octahedral.glsl"
#include "ibl.glsl"

layout(set = 0, binding = 0) buffer MeshData {
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
bool isNull(Material material);
ClearCoat getClearCoat();
vec2 transformUV(TextureInfo textureInfo);

layout(location = 0) out vec4 fragColor;

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;

NormalInfo ni = NormalInfo(fs_in.tangent, fs_in.bitangent, fs_in.normal);

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
    const float metalness = clamp(mro.r, 0, 1);
    const float roughness = clamp(mro.g, 0, 1);
    const float ao = mro.b;
    const float alphaRoughness = roughness * roughness;
    const vec3 f0 = mix(F0, baseColor.rgb, metalness);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float ior = MATERIAL.ior;
    const float specularWeight = 1;
    const float thickness = getThickness();
    const vec3 attenuationColor = MATERIAL.attenuationColor;
    const float attenuationDistance = MATERIAL.attenuationDistance;
    const float dispersion = MATERIAL.dispersion;
    const vec3 emission = getEmission();
    const ClearCoat cc = getClearCoat();


    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);
    vec3 f_emissive = vec3(0.0);
    vec3 f_clearcoat = vec3(0.0);
    vec3 f_sheen = vec3(0.0);
    vec3 f_transmission = vec3(0.0);
    float albedoSheenScaling = 1.0;


    vec3 normal = getNormal();
    vec3 N = normalize(normal);
    vec3 V = normalize(fs_in.eyes - fs_in.position);
    f_emissive = emission;


    if(ibl_on == 1){
        f_specular += getIBLRadianceGGX(N, V, roughness, f0, specularWeight);
        f_diffuse += getIBLRadianceLambertian(N, V, roughness, c_diff, f0, specularWeight);
        f_clearcoat += cc.factor != 0 ? getIBLRadianceGGX(cc.normal, V, cc.roughness, cc.f0, 1.0) : vec3(0);
    }

    if(transmissionFactor > 0){
        baseColor.a = 1;
        f_transmission += getIBLVolumeRefraction(N, V, roughness, c_diff, f0, f90, fs_in.position, MODEL_MATRIX, view, projection
        , ior, thickness, attenuationColor, attenuationDistance, dispersion);
    }

    vec3 f_diffuse_ibl = f_diffuse;
    vec3 f_specular_ibl = f_specular;
    vec3 f_clearcoat_ibl = f_clearcoat;

    f_diffuse = vec3(0);
    f_specular = vec3(0);
    f_clearcoat = vec3(0);

    if(direct_on == 1){
        for (int i = 0; i < num_lights; ++i){
            Light light = lightAt(i);

            vec3 pointToLight;
            if (light.type != LightType_Directional){
                pointToLight = light.position - fs_in.position;
            }
            else {
                pointToLight = -light.direction;
            }

            // BSTF
            vec3 L = normalize(pointToLight);// Direction from surface point to light
            vec3 H = normalize(L + V);// Direction of the vector between L and V, called halfway vector
            float NdotL = clampedDot(N, L);
            float NdotV = clampedDot(N, V);
            float NdotH = clampedDot(N, H);
            float LdotH = clampedDot(L, H);
            float VdotH = clampedDot(V, H);

            if (NdotL > 0.0 || NdotV > 0.0){
                vec3 intensity = getLighIntensity(light, pointToLight);
                vec3 l_diffuse = vec3(0.0);
                vec3 l_specular = vec3(0.0);

                l_diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
                l_specular += intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);

                f_diffuse += l_diffuse;
                f_specular += l_specular;

                f_clearcoat += cc.factor > 0 ? intensity * getPunctualRadianceClearCoat(cc.normal, V, L, H, VdotH, cc.f0, cc.f90, cc.roughness) : vec3(0);
            }
            // If the light ray travels through the geometry, use the point it exits the geometry again.
            // That will change the angle to the light source, if the material refracts the light ray.

            if (transmissionFactor > 0){
                vec3 transmissionRay = getVolumeTransmissionRay(N, V, thickness, ior, MODEL_MATRIX);
                pointToLight -= transmissionRay;
                L = normalize(pointToLight);

                vec3 intensity = getLighIntensity(light, pointToLight);
                vec3 transmittedLight = intensity * getPunctualRadianceTransmission(N, V, L, alphaRoughness, f0, f90, c_diff, ior);
                transmittedLight = applyVolumeAttenuation(transmittedLight, length(transmissionRay), attenuationColor, attenuationDistance);
            }

        }
    }

    float clearcoatFactor = 0.0;
    vec3 clearcoatFresnel = vec3(0);
    vec3 diffuse;
    vec3 specular;
    vec3 clearcoat;

    diffuse = f_diffuse + mix(f_diffuse_ibl, f_diffuse_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;
    specular = f_specular + mix(f_specular_ibl, f_specular_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;
    clearcoat = f_clearcoat + mix(f_clearcoat_ibl, f_clearcoat_ibl * ao, u_OcclusionStrength);

    clearcoatFactor = cc.factor;
    clearcoatFresnel = F_Schlick(cc.f0, cc.f90, clampedDot(cc.normal, V));
    clearcoat *= clearcoatFactor;

    diffuse = mix(diffuse, f_transmission, transmissionFactor);

    vec3 color = vec3(0);
    color = f_emissive + diffuse + specular;
    color = color * (1.0 - clearcoatFactor * clearcoatFresnel) + clearcoat;

    fragColor = vec4(color, baseColor.a);

    if(debug == 1) {
        fragColor = vec4(baseColor.rgb, 1);
    }
    if(debug == 2) {
        fragColor = vec4(normal, 1);
    }
    if(debug == 3) {
        fragColor = vec4(vec3(roughness), 1);
    }
    if(debug == 4) {
        fragColor = vec4(vec3(metalness), 1);
    }

    if(debug == 5) {
        fragColor = vec4(vec3(ao), 1);
    }

    if(debug == 6) {
        fragColor = vec4(emission, 1);
    }
}

float saturate(float x) {
    return clamp(x, 0, 1);
}

vec4 getBaseColor() {
    if(BASE_COLOR_TEX_INFO.index == -1){
        return isNull(MATERIAL) ? fs_in.color : vec4(MATERIAL.baseColor);
    }
    vec2 uv = transformUV(BASE_COLOR_TEX_INFO);
    vec4 color = texture(BASE_COLOR_TEXTURE, uv);
    color.rgb = pow(color.rgb, vec3(2.2));
    return color;
}

vec3 getMRO() {
    vec3 mro;
    mro.r = MATERIAL.metalness;
    mro.g = MATERIAL.roughness;
    mro.b = 1;


    if(METAL_ROUGHNESS_TEX_INFO.index != -1) {
        vec2 uv = transformUV(METAL_ROUGHNESS_TEX_INFO);
        vec3 res = texture(METAL_ROUGHNESS_TEXTURE, uv).rgb;
        mro.r *= res.b;
        mro.g *= res.g;

        if(OCCLUSION_TEX_INFO.index != -1) {
            if(OCCLUSION_TEX_INFO.index == METAL_ROUGHNESS_TEX_INFO.index) {
                mro.b = res.r;
            }else {
                mro.b = texture(OCCLUSION_TEXTURE, uv).r;
            }
        }
    }
    return mro;
}

vec3 getNormal() {
    if(NORMAL_TEX_INFO.index == -1 || noTangets()) {
        return fs_in.normal;
    }

    vec2 uv = transformUV(NORMAL_TEX_INFO);
    mat3 TBN = mat3(fs_in.tangent, fs_in.bitangent, fs_in.normal);

    vec3 nScale = vec3(NORMAL_TEX_INFO.tScale, NORMAL_TEX_INFO.tScale, 1);
    vec3 normal = 2 * texture(NORMAL_TEXTURE, uv).xyz - 1;
    normal *= nScale;
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
    vec3 emission = MATERIAL.emission * MATERIAL.emissiveStrength;
    if(EMISSION_TEX_INFO.index != -1) {
        vec2 uv = transformUV(EMISSION_TEX_INFO);
        emission *= pow(texture(EMISSION_TEXTURE, uv).rgb, vec3(2.2));
    }
    return emission;
}

float getTransmissionFactor() {
    return MATERIAL.transmission;
}

float getThickness() {
    float thickness = MATERIAL.thickness;
    if(THICKNESS_TEX_INFO.index != -1){
        vec2 uv = transformUV(THICKNESS_TEX_INFO);
        thickness *= texture(THICKNESS_TEXTURE, uv).g;
    }
    return thickness;
}

bool isNull(Material material) {
    return any(isnan(material.baseColor));
}

ClearCoat getClearCoat() {
    ClearCoat cc = newClearCoatInstance();
    vec2 uv;

    if(MATERIAL.clearCoatFactor == 0) return cc;
    cc.factor = MATERIAL.clearCoatFactor;
    cc.roughness = MATERIAL.clearCoatRoughnessFactor;
    cc.f0 = vec3(pow((MATERIAL.ior - 1.0) / (MATERIAL.ior + 1.0), 2.0));
    cc.f90 = vec3(1);
    cc.normal = ni.N;

    if(CLEAR_COAT_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_TEX_INFO);
        cc.factor *= texture(CLEAR_COAT_TEXTURE, uv).r;
    }

    if(CLEAR_COAT_ROUGHNESS_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_ROUGHNESS_TEX_INFO);
        cc.roughness *= texture(CLEAR_COAT_ROUGHNESS_TEXTURE, uv).g;
    }


    if(CLEAR_COAT_NORMAL_TEX_INFO.index != -1) {
        uv = transformUV(CLEAR_COAT_NORMAL_TEX_INFO);
        mat3 TBN = mat3(ni.T, ni.B, ni.N);
        cc.normal = 2 * texture(CLEAR_COAT_NORMAL_TEXTURE, uv).xyz - 1;
        cc.normal.y *= -1;
        cc.normal = normalize(TBN * cc.normal);
    }

    cc.normal = normalize(cc.normal);
    cc.roughness = clamp(cc.roughness, 0, 1);

    return cc;
}

vec2 transformUV(TextureInfo ti) {
    mat3 translation = mat3(1,0,0, 0,1,0, ti.offset.x, ti.offset.y, 1);
    mat3 rotation = mat3(
        cos(ti.rotation), -sin(ti.rotation), 0,
        sin(ti.rotation), cos(ti.rotation), 0,
        0,             0, 1
    );
    mat3 scale = mat3(ti.scale.x,0,0, 0,ti.scale.y,0, 0,0,1);

    mat3 matrix = translation * rotation * scale;
    return ( matrix * vec3(fs_in.uv, 1) ).xy;
}