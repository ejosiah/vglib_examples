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
#define TEXTURE_INFO_PER_MATERIAL 20

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define TEXTURE_OFFSET (MATERIAL.textureInfoOffset * TEXTURE_INFO_PER_MATERIAL)

#define BASE_COLOR_TEX_INFO textureInfos[TEXTURE_OFFSET + BASE_COLOR_INDEX]
#define NORMAL_TEX_INFO textureInfos[TEXTURE_OFFSET + NORMAL_INDEX]
#define METAL_ROUGHNESS_TEX_INFO textureInfos[TEXTURE_OFFSET + METALLIC_ROUGHNESS_INDEX]
#define OCCLUSION_TEX_INFO textureInfos[TEXTURE_OFFSET + OCCLUSION_INDEX]

#define EMISSION_TEX_INFO textureInfos[TEXTURE_OFFSET + EMISSION_INDEX]

#define BASE_COLOR_TEXTURE global_textures[nonuniformEXT(BASE_COLOR_TEX_INFO.index)]
#define NORMAL_TEXTURE global_textures[nonuniformEXT(NORMAL_TEX_INFO.index)]
#define METAL_ROUGHNESS_TEXTURE global_textures[nonuniformEXT(METAL_ROUGHNESS_TEX_INFO.index)]
#define OCCLUSION_TEXTURE global_textures[nonuniformEXT(OCCLUSION_TEX_INFO.index)]


#include "gltf.glsl"
#include "lighting.glsl"

layout(set = 0, binding = 0, scalar) buffer MeshData {
    Mesh meshes[];
};

layout(set = 1, binding = 0) buffer GLTF_MATERIAL {
    Material materials[];
};

layout(set = 1, binding = 3) buffer TextureInfos {
    TextureInfo textureInfos[];
};


layout(set = 2, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv[2];
} fs_in;

layout(location = 8) in flat int drawId;

float saturate(float x);
vec4 getBaseColor();
vec3 getNormal();
vec3 getMRO();
bool hasTanget();
bool hasNormal();
vec2 transformUV(TextureInfo textureInfo);

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

    const vec3 lightDir = fs_in.eyes - fs_in.position;
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
    vec4 color =  MATERIAL.baseColor;
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

vec3 getNormal() {

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
    if (gl_FrontFacing == false && MATERIAL.doubleSided == 1)
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
    return info.N;

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


bool hasTanget() {
    return !all(equal(fs_in.tangent, vec3(0)));
}

bool hasNormal() {
    return !all(equal(fs_in.normal, vec3(0)));
}
