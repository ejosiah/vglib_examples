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

#define UNIFORMS_SET 3
#define UNIFORMS_BINDING_POINT 0

layout(set = 2, binding = 10) uniform sampler2D global_textures[];

#include "gltf_brdf.glsl"
#include "uniforms.glsl"
#include "punctual_lights.glsl"
#include "gltf.glsl"
#include "octahedral.glsl"
#include "ibl.glsl"
#include "iridescence.glsl"

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
    vec2 uv[2];
} fs_in;

layout(location = 13) in flat int drawId;

float saturate(float x);

layout(location = 0) out vec4 fragColor;

const vec3 F0 = vec3(0.04);
const vec3 F90 = vec3(1);
const float IOR = 1.5;
const float u_OcclusionStrength = 1;

NormalInfo ni;

void main() {

    vec4 baseColor = getBaseColor();

    if(MATERIAL.alphaMode == ALPHA_MODE_MASK)  {
        if(baseColor.a < MATERIAL.alphaCutOff){
            discard;
        }
        baseColor.a = 1;
    }

    if(MATERIAL.unlit == 1) {
        fragColor = baseColor;
        return;
    }

    const float transmissionFactor = getTransmissionFactor();

    if((MATERIAL.alphaMode == ALPHA_MODE_BLEND || transmissionFactor > 0) && discard_transmissive == 1) {
        discard;
    }

    ni = getNormalInfo();
    const Specular spec = getSpecular();
    const vec3 mro = getMRO();
    const float metalness = clamp(mro.r, 0, 1);
    const float perceptualRoughness = clamp(mro.g, 0, 1);
    const float ao = mro.b;
    const float alphaRoughness = perceptualRoughness * perceptualRoughness;
    const vec3 dielectricSpecularF0 = min(F0 * spec.color, vec3(1));
    const vec3 f0 = mix(dielectricSpecularF0, baseColor.rgb, metalness);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float ior = MATERIAL.ior;
    const float specularWeight = spec.factor;
    const float thickness = getThickness();
    const vec3 attenuationColor = MATERIAL.attenuationColor;
    const float attenuationDistance = MATERIAL.attenuationDistance;
    const float dispersion = MATERIAL.dispersion;
    const vec3 emission = getEmission();
    const ClearCoat cc = getClearCoat();
    const Sheen sheenMat = getSheen();
    const Anisotropy anisotropy = getAnisotropy();
    Iridescence iridescence = getIridescence();


    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);
    vec3 f_emissive = vec3(0.0);
    vec3 f_clearcoat = vec3(0.0);
    vec3 f_sheen = vec3(0.0);
    vec3 f_transmission = vec3(0.0);
    float albedoSheenScaling = 1.0;


    vec3 normal = ni.N;
    vec3 N = normalize(normal);
    vec3 V = normalize(fs_in.eyes - fs_in.position);

    float NdotV = clampedDot(N, V);
    float TdotV = clampedDot(ni.T, V);
    float BdotV = clampedDot(ni.B, V);

    vec3 iridescenceFresnel = evalIridescence(1.0, iridescence.ior, NdotV, iridescence.thickness, f0);
    vec3 iridescenceF0 = Schlick_to_F0(iridescenceFresnel, NdotV);

    if (iridescence.thickness == 0.0) {
        iridescence.factor = 0.0;
    }

    f_emissive = emission;


    if(ibl_on == 1){
        if(iridescence.enabled){
            f_specular += getIBLRadianceGGXIridescence(N, V, perceptualRoughness, f0, iridescenceFresnel, iridescence.factor, specularWeight);
            f_diffuse += getIBLRadianceLambertianIridescence(N, V, perceptualRoughness, c_diff, f0, f90,  iridescence.factor, specularWeight);
        }else if(anisotropy.enabled) {
            f_specular += getIBLRadianceAnisotropy(N, V, perceptualRoughness, anisotropy.strength, anisotropy.bitangent, f0, specularWeight);
            f_diffuse += getIBLRadianceLambertian(N, V, perceptualRoughness, c_diff, f0, specularWeight);
        }else {
            f_specular += getIBLRadianceGGX(N, V, perceptualRoughness, f0, specularWeight);
            f_diffuse += getIBLRadianceLambertian(N, V, perceptualRoughness, c_diff, f0, specularWeight);
        }

        f_clearcoat += cc.enabled ? getIBLRadianceGGX(cc.normal, V, cc.roughness, cc.f0, 1.0) : vec3(0);

        if(sheenMat.enabled){
            f_sheen += getIBLRadianceCharlie(N, V, sheenMat.roughness, sheenMat.color);
            albedoSheenScaling = 1.0 - max3(sheenMat.color) * albedoSheenScalingLUT(NdotV, sheenMat.roughness);
        }
    }

    if(transmissionFactor > 0){
        baseColor.a = 1;
        f_transmission += getIBLVolumeRefraction(N, V, perceptualRoughness, c_diff, f0, f90, fs_in.position, MODEL_MATRIX, view, projection
        , ior, thickness, attenuationColor, attenuationDistance, dispersion);
    }

    vec3 f_diffuse_ibl = f_diffuse;
    vec3 f_specular_ibl = f_specular;
    vec3 f_sheen_ibl = f_sheen;
    vec3 f_clearcoat_ibl = f_clearcoat;

    f_diffuse = vec3(0);
    f_specular = vec3(0);
    f_clearcoat = vec3(0);
    f_sheen = vec3(0);

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

                 if(iridescence.enabled) {
                     l_diffuse += intensity * NdotL *  BRDF_lambertianIridescence(f0, f90, iridescenceFresnel, iridescence.factor, c_diff, specularWeight, VdotH);
                     l_specular += intensity * NdotL * BRDF_specularGGXIridescence(f0, f90, iridescenceFresnel, alphaRoughness, iridescence.factor, specularWeight, VdotH, NdotL, NdotV, NdotH);

                 } else  if(anisotropy.enabled){
                    l_diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
                    l_specular += intensity * NdotL * BRDF_specularGGXAnisotropy(f0, f90, alphaRoughness, anisotropy.strength, N, V, L, H, anisotropy.tangent, anisotropy.bitangent);
                }else {
                    l_diffuse += intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
                    l_specular += intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);
                }

                f_diffuse += l_diffuse;
                f_specular += l_specular;

                f_clearcoat += cc.factor > 0 ? intensity * getPunctualRadianceClearCoat(cc.normal, V, L, H, VdotH, cc.f0, cc.f90, cc.roughness) : vec3(0);

                if(sheenMat.enabled) {
                    f_sheen += intensity * getPunctualRadianceSheen(sheenMat.color, sheenMat.roughness, NdotL, NdotV, NdotH);
                    float l_albedoSheenScaling = min(1.0 - max3(sheenMat.color) * albedoSheenScalingLUT(NdotV, sheenMat.roughness),
                    1.0 - max3(sheenMat.color) * albedoSheenScalingLUT(NdotL, sheenMat.roughness));
                    l_diffuse *= l_albedoSheenScaling;
                    l_specular *= l_albedoSheenScaling;
                }
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
    vec3 sheen;
    vec3 clearcoat;

    diffuse = f_diffuse + mix(f_diffuse_ibl, f_diffuse_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;
    specular = f_specular + mix(f_specular_ibl, f_specular_ibl * ao, u_OcclusionStrength) * albedoSheenScaling;
    sheen = f_sheen + mix(f_sheen_ibl, f_sheen_ibl * ao, u_OcclusionStrength);
    clearcoat = f_clearcoat + mix(f_clearcoat_ibl, f_clearcoat_ibl * ao, u_OcclusionStrength);

    clearcoatFactor = cc.factor;
    clearcoatFresnel = F_Schlick(cc.f0, cc.f90, clampedDot(cc.normal, V));
    clearcoat *= clearcoatFactor;

    diffuse = mix(diffuse, f_transmission, transmissionFactor);

    vec3 color = vec3(0);
    color = f_emissive + diffuse + specular;
    color += sheen;
    color = color * (1.0 - clearcoatFactor * clearcoatFresnel) + clearcoat;

    fragColor = vec4(color, baseColor.a);

    if(debug == 1) {
        fragColor = vec4(baseColor.rgb, 1);
    }
    if(debug == 2) {
        fragColor = vec4(normal, 1);
    }
    if(debug == 3) {
        fragColor = vec4(vec3(metalness), 1);
    }
    if(debug == 4) {
        fragColor = vec4(vec3(perceptualRoughness), 1);
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
    vec4 color = fs_in.color * MATERIAL.baseColor;
    if (BASE_COLOR_TEX_INFO.index != -1){
        vec2 uv = transformUV(BASE_COLOR_TEX_INFO);
        vec4 texColor = texture(BASE_COLOR_TEXTURE, uv);
        texColor.rgb = pow(texColor.rgb, vec3(2.2));
        color *= texColor;
    }

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
    }

    if(OCCLUSION_TEX_INFO.index != -1) {
        vec2 uv = transformUV(OCCLUSION_TEX_INFO);
        mro.b = texture(OCCLUSION_TEXTURE, uv).r;
    }
    return mro;
}

NormalInfo getNormalInfo() {

    vec2 uv = transformUV(NORMAL_TEX_INFO);
    vec2 uv_dx = dFdx(uv);
    vec2 uv_dy = dFdy(uv);

    if (length(uv_dx) <= 1e-2) {
        uv_dx = vec2(1.0, 0.0);
    }

    if (length(uv_dy) <= 1e-2) {
        uv_dy = vec2(0.0, 1.0);
    }

    vec3 t_ = (uv_dy.t * dFdx(fs_in.position) - uv_dx.t * dFdy(fs_in.position)) /
    (uv_dx.s * uv_dy.t - uv_dy.s * uv_dx.t);

    vec3 n, t, b, ng;

    // Compute geometrical TBN:
    if(hasNormal()){
        if (hasTanget()){
            // Trivial TBN computation, present as vertex attribute.
            // Normalize eigenvectors as matrix is linearly interpolated.
            t = normalize(fs_in.tangent);
            b = normalize(fs_in.bitangent);
            ng = normalize(fs_in.normal);
        } else {
            // Normals are either present as vertex attributes or approximated.
            ng = normalize(fs_in.normal);
            t = normalize(t_ - ng * dot(ng, t_));
            b = cross(ng, t);
        }
    } else {
        ng = normalize(cross(dFdx(fs_in.position), dFdy(fs_in.position)));
        t = normalize(t_ - ng * dot(ng, t_));
        b = cross(ng, t);
    }


    // For a back-facing surface, the tangential basis vectors are negated.
    if (gl_FrontFacing == false)
    {
        t *= -1.0;
        b *= -1.0;
        ng *= -1.0;
    }

    // Compute normals:
    NormalInfo info;
    info.Ng = ng;
    if(NORMAL_TEX_INFO.index != -1){
        info.Ntex = texture(NORMAL_TEXTURE, uv).rgb * 2.0 - vec3(1.0);
        info.Ntex *= vec3(NORMAL_TEX_INFO.tScale, NORMAL_TEX_INFO.tScale, 1.0);
        info.Ntex = normalize(info.Ntex);
        info.N = normalize(mat3(t, b, ng) * info.Ntex);
    } else {
        info.N = ng;
    }
    info.T = t;
    info.B = b;
    return info;

}

bool hasTanget() {
    return !all(equal(fs_in.tangent, vec3(0)));
}

bool hasNormal() {
    return !all(equal(fs_in.normal, vec3(0)));
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
    float transmission = MATERIAL.transmission;
    if(TRANSMISSION_TEX_INFO.index == -1) return transmission;

    vec2 uv = transformUV(TRANSMISSION_TEX_INFO);
    transmission *= texture(TRANSMISSION_TEXTURE, uv).r;

    return transmission;
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
        cc.normal = normalize(TBN * cc.normal);
    }

    cc.normal = normalize(cc.normal);
    cc.roughness = clamp(cc.roughness, 0, 1);
    cc.enabled = cc.factor != 0;

    return cc;
}

Sheen getSheen() {
    Sheen sheen = newSheenInstance();
    sheen.color = MATERIAL.sheenColorFactor;
    sheen.roughness = MATERIAL.sheenRoughnessFactor;

    if(SHEEN_COLOR_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SHEEN_COLOR_TEX_INFO);
        sheen.color *= pow(texture(SHEEN_COLOR_TEXTURE, uv).rgb, vec3(2.2));
    }

    if(SHEEN_ROUGHNESS_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SHEEN_ROUGHNESS_TEX_INFO);
        sheen.roughness *= texture(SHEEN_ROUGHNESS_TEXTURE, uv).a;
    }

    sheen.enabled = all(notEqual(sheen.color, vec3(0))) || sheen.roughness != 0;

    return sheen;
}

Anisotropy getAnisotropy() {
    Anisotropy anisotropy = newAnisotropyInstance();

    float strength = MATERIAL.anisotropyStrength;
    vec2 rotationDirection = MATERIAL.anisotropyRotation;

    vec2 direction = vec2(1, 0);

    if(ANISOTROPY_TEX_INFO.index != -1){
        vec2 uv = transformUV(ANISOTROPY_TEX_INFO);
        vec3 anisotropySample = texture(ANISOTROPY_TEXTURE, uv).xyz;
        direction = 2 * anisotropySample.xy - 1;
        strength *= anisotropySample.z;
    }

    mat2 rotator = mat2(rotationDirection.x, rotationDirection.y, -rotationDirection.y, rotationDirection.x);
    direction = rotator * normalize(direction);

    anisotropy.tangent = normalize(mat3(ni.T, ni.B, ni.Ng) * vec3(direction, 0.0));
    anisotropy.bitangent = normalize(cross(ni.Ng, anisotropy.tangent));
    anisotropy.strength = clamp(strength, 0, 1);

    anisotropy.enabled = strength > 0;
    return anisotropy;
}

Specular getSpecular() {
    Specular specular = newSpecluarInstance();

    specular.color = MATERIAL.specularColor;
    specular.factor = MATERIAL.specularFactor;

    if(SPECULAR_STRENGTH_TEX_INFO.index != -1) {
        vec2 uv = transformUV(SPECULAR_STRENGTH_TEX_INFO);
        specular.factor *= texture(SPECULAR_STRENGTH_TEXTURE, uv).a;
    }

    if(SPECULAR_COLOR_TEX_INFO.index != -1){
        vec2 uv = transformUV(SPECULAR_COLOR_TEX_INFO);
        specular.color *= texture(SPECULAR_COLOR_TEXTURE, uv).rgb;
    }
    return specular;
}

Iridescence getIridescence() {
    Iridescence iri = newIridescencInstance();

    iri.factor = MATERIAL.iridescenceFactor;
    iri.ior = MATERIAL.iridescenceIor;
    iri.thickness = MATERIAL.iridescenceThicknessMaximum;

    if(IRIDESCENCE_TEX_INFO.index != -1) {
        vec2 uv = transformUV(IRIDESCENCE_TEX_INFO);
        iri.factor *= texture(IRIDESCENCE_TEXTURE, uv).r;
    }

    if(IRIDESCENCE_THICKNESS_TEX_INFO.index != -1){
        vec2 uv = transformUV(IRIDESCENCE_THICKNESS_TEX_INFO);
        float g = texture(IRIDESCENCE_THICKNESS_TEXTURE, uv).g;
        iri.thickness = mix(MATERIAL.iridescenceThicknessMinimum, iri.thickness, g);
    }

    iri.enabled = iri.factor > 0;

    return iri;
}

vec2 transformUV(TextureInfo ti) {
    if(ti.index == -1) return fs_in.uv[0];

    mat3 translation = mat3(1,0,0, 0,1,0, ti.offset.x, ti.offset.y, 1);
    mat3 rotation = mat3(
        cos(ti.rotation), -sin(ti.rotation), 0,
        sin(ti.rotation), cos(ti.rotation), 0,
        0,             0, 1
    );
    mat3 scale = mat3(ti.scale.x,0,0, 0,ti.scale.y,0, 0,0,1);

    mat3 matrix = translation * rotation * scale;
    return ( matrix * vec3(fs_in.uv[ti.texCoord], 1) ).xy;
}