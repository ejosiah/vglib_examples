#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "gltf_brdf.glsl"
#include "uniforms.glsl"

layout(set = 1, binding = 0) uniform sampler2D environment;
layout(set = 1, binding = 1) uniform sampler2D env_specular;
layout(set = 1, binding = 2) uniform sampler2D env_irradiance;
layout(set = 1, binding = 3) uniform sampler2D brdfLUT;

layout(set = 2, binding = 0, scalar) uniform Material {
    vec3 diffuse;
    vec3 ambient;
    vec3 specular;
    vec3 emission;
    vec3 transmittance;
    float shininess;
    float ior;
    float opacity;
    float illum;

    uint diffuseTex;
    uint ambientTex;
    uint specularTex;
    uint normalTex;
    uint ambientOcclusionTex;
} material;

layout(set = 2, binding = 1) uniform sampler2D model_textures[];

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec2 uv;
} fs_in;


layout(location = 0) out vec3 specular;
layout(location = 1) out vec4 diffuse;

 uint u_MipCount = textureQueryLevels(env_specular);

vec3 getIBLRadianceGGX(vec3 n, vec3 v, float roughness, vec3 F0, float specularWeight);

vec3 getIBLRadianceLambertian(vec3 n, vec3 v, float roughness, vec3 diffuseColor, vec3 F0, float specularWeight);

const vec3 F0 = vec3(0.04);

#define bumpMap model_textures[material.normalTex]

vec3 computeBumpNormalScreenSpace(vec2 uv, float bumpScale) {
    vec3 bump = -1 + 2 * texture(bumpMap, uv).rgb;
    return mix(vec3(0,0,1), bump, bumpScale);
}

vec3 bumpToNormal(vec2 uv, float bumpScale) {
    const float bumpStrength = 25;
    // Sample the bump map at the current texture coordinate
    float heightL = texture(bumpMap, uv + vec2(-1.0, 0.0) / textureSize(bumpMap, 0)).r;
    float heightR = texture(bumpMap, uv + vec2(1.0, 0.0) / textureSize(bumpMap, 0)).r;
    float heightD = texture(bumpMap, uv + vec2(0.0, -1.0) / textureSize(bumpMap, 0)).r;
    float heightU = texture(bumpMap, uv + vec2(0.0, 1.0) / textureSize(bumpMap, 0)).r;

    // Calculate the gradients (dx, dy) with added bump strength factor
    float dx = (heightR - heightL) * bumpStrength;
    float dy = (heightU - heightD) * bumpStrength;

    // The normal direction in 3D space, using the gradient
    vec3 normal = normalize(vec3(-dx, -dy, 1.0));

    // Remap the normal from [-1, 1] range to [0, 1] range for normal map encoding
    // normal = normal * 0.5 + 0.5;

    return mix(vec3(0, 0, 1), normal, bumpScale);
//    return texture(bumpMap, uv).rgb;
}

mat3 calculateTBN( vec3 N, vec3 p, vec2 uv )
{
    // get edge vectors of the pixel triangle
    vec3 dp1 = dFdx( p );
    vec3 dp2 = dFdy( p );
    vec2 duv1 = dFdx( uv );
    vec2 duv2 = dFdy( uv );

    // solve the linear system
    vec3 dp2perp = cross( dp2, N );
    vec3 dp1perp = cross( N, dp1 );
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame
    float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
    return mat3( T * -invmax, B * invmax, N );
}


void main() {
    vec4 baseColor = texture(model_textures[material.diffuseTex], fs_in.uv);
    vec3 specularAO = texture(model_textures[material.ambientTex], fs_in.uv).rgb;

    const float metalness = 0;
    const float sIntensity = specularAO.r * uniforms.specularIntensity;
    const float perceptualRoughness = (specularAO.g / 0.3) * uniforms.specularRoughness;
    const float ao = specularAO.b;
    const float alphaRoughness = perceptualRoughness * perceptualRoughness;
    const vec3 f0 = F0;
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float specularWeight = 1;

    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);


    mat3 TBN = calculateTBN(fs_in.normal, fs_in.position, fs_in.uv);
    vec3 normal = bumpToNormal(fs_in.uv, uniforms.bumpiness);
    vec3 N = normalize(TBN * normal);
    vec3 V = normalize(fs_in.eyes - fs_in.position);

    f_specular += getIBLRadianceGGX(N, V, perceptualRoughness, f0, specularWeight);
    f_diffuse += getIBLRadianceLambertian(N, V, perceptualRoughness, c_diff, f0, specularWeight);

    vec3 f_diffuse_ibl = f_diffuse;
    vec3 f_specular_ibl = f_specular;

    f_diffuse = vec3(0);
    f_specular = vec3(0);

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
        vec3 l_diffuse = intensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
        vec3 l_specular = intensity * NdotL * BRDF_specularGGX(f0, f90, alphaRoughness, specularWeight, VdotH, NdotL, NdotV, NdotH);

        f_diffuse += l_diffuse;
        f_specular += l_specular * sIntensity;
    }


    vec3 ambient = ao * baseColor.rgb + f_diffuse_ibl;
    diffuse.rgb =  f_diffuse + ambient * uniforms.ambientFactor;
    specular =  f_specular + (ao * f_specular_ibl) * uniforms.ambientFactor;

//    diffuse.rgb = baseColor.rgb * NdotL;
//    specular = vec3(0);

    diffuse.a = sssEnaled() ? baseColor.a : 1;
}

vec2 dirToUV(vec3 dir) {
    return 0.5 + 0.5 * octEncode(normalize(dir));
}

vec4 getSpecularSample(vec3 reflection, float lod) {
    return textureLod(env_specular, dirToUV(u_EnvRotation * reflection), lod);
}


vec3 getIBLRadianceGGX(vec3 n, vec3 v, float roughness, vec3 F0, float specularWeight) {
    float NdotV = clamp(dot(n, v), 0, 1);
    float lod = roughness * float(u_MipCount - 1);
    vec3 reflection = normalize(reflect(-v, n));

    vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec2 f_ab = texture(brdfLUT, brdfSamplePoint).rg;
    vec4 specularSample = getSpecularSample(reflection, lod);

    vec3 specularLight = specularSample.rgb;

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera
    vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
    vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    vec3 FssEss = k_S * f_ab.x + f_ab.y;

    return specularWeight * specularLight * FssEss;
}

vec3 getDiffuseLight(vec3 n) {
    return texture(env_irradiance, dirToUV(u_EnvRotation * n)).rgb ;
}


vec3 getIBLRadianceLambertian(vec3 n, vec3 v, float roughness, vec3 diffuseColor, vec3 F0, float specularWeight) {
    float NdotV = clamp(dot(n, v), 0, 1);
    vec2 brdfSamplePoint = clamp(vec2(NdotV, roughness), vec2(0.0, 0.0), vec2(1.0, 1.0));
    vec2 f_ab = texture(brdfLUT, brdfSamplePoint).rg;

    vec3 irradiance = getDiffuseLight(n);

    // see https://bruop.github.io/ibl/#single_scattering_results at Single Scattering Results
    // Roughness dependent fresnel, from Fdez-Aguera

    vec3 Fr = max(vec3(1.0 - roughness), F0) - F0;
    vec3 k_S = F0 + Fr * pow(1.0 - NdotV, 5.0);
    vec3 FssEss = specularWeight * k_S * f_ab.x + f_ab.y; // <--- GGX / specular light contribution (scale it down if the specularWeight is low)

    // Multiple scattering, from Fdez-Aguera
    float Ems = (1.0 - (f_ab.x + f_ab.y));
    vec3 F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0);
    vec3 FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);
    vec3 k_D = diffuseColor * (1.0 - FssEss + FmsEms); // we use +FmsEms as indicated by the formula in the blog post (might be a typo in the implementation)

    return (FmsEms + k_D) * irradiance;
}