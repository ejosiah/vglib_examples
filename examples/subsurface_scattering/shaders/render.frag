#version 460

#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "gltf_brdf.glsl"
#include "uniforms.glsl"

#define bumpMap model_textures[material.normalTex]
#define u_shadowMap global_textures[light.shadowMapIndex]

// TODO move these to bindness
layout(set = 1, binding = 0) uniform sampler2D environment;
layout(set = 1, binding = 1) uniform sampler2D env_specular;
layout(set = 1, binding = 2) uniform sampler2D env_irradiance;
layout(set = 1, binding = 3) uniform sampler2D brdfLUT;
layout(set = 1, binding = 4) uniform sampler2D beckmannLUT;
layout(set = 2, binding = 10) uniform sampler2D global_textures[];


layout(set = 3, binding = 0, scalar) uniform Material {
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

layout(set = 3, binding = 1) uniform sampler2D model_textures[];

layout(location = 0) in struct {
    vec4 lightSpacePosition;
    vec3 position;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec2 uv;
} fs_in;



vec3 getIBLRadianceGGX(vec3 n, vec3 v, float roughness, vec3 F0, float specularWeight);
vec3 getIBLRadianceLambertian(vec3 n, vec3 v, float roughness, vec3 diffuseColor, vec3 F0, float specularWeight);
vec3 computeBumpNormalScreenSpace(vec2 uv, float bumpScale);
vec3 bumpToNormal(vec2 uv, float bumpScale);
mat3 calculateTBN( vec3 N, vec3 p, vec2 uv );
float pcfFilteredShadow(vec4 lightSpacePos);
vec3 SSSTransmittance(float translucency, float sssWidth, vec3 worldPosition, vec3 worldNormal, vec3 light, sampler2D shadowMap, mat4 lightViewProjection, float lightFarPlane);
float linearizeDepth1(float z);

float fresnel(vec3 halfV, vec3 view, float f0);
float specularKSK(sampler2D beckmannTex, vec3 normal, vec3 light, vec3 view, float roughness);

uint u_MipCount = textureQueryLevels(env_specular);
const float F0 = 0.028;

layout(location = 0) out vec3 specular;
layout(location = 1) out vec4 diffuse;

void main() {
    vec4 baseColor = texture(model_textures[material.diffuseTex], fs_in.uv);
    vec3 specularAO = texture(model_textures[material.ambientTex], fs_in.uv).rgb;

    const float metalness = 0;
    const float sIntensity = specularAO.r * uniforms.specularIntensity;
    const float roughness = uniforms.specularRoughness;
    const float sRoughness = (specularAO.g / 0.3) * uniforms.specularRoughness;
    const float ao = specularAO.b;
    const float alphaRoughness = roughness * roughness;
    const vec3 f0 = vec3(F0);
    const vec3 f90 = vec3(1);
    const vec3 c_diff = mix(baseColor.rgb, vec3(0), metalness);
    const float specularWeight = 1;

    vec3 f_specular = vec3(0.0);
    vec3 f_diffuse = vec3(0.0);


    mat3 TBN = calculateTBN(fs_in.normal, fs_in.position, fs_in.uv);
    vec3 normal = bumpToNormal(fs_in.uv, uniforms.bumpiness);
    vec3 N = normalize(TBN * normal);
    vec3 V = normalize(fs_in.eyes - fs_in.position);

    f_specular += getIBLRadianceGGX(N, V, roughness, f0, specularWeight);
    f_diffuse += getIBLRadianceLambertian(N, V, roughness, c_diff, f0, specularWeight);

    vec3 f_diffuse_ibl = f_diffuse;
    vec3 f_specular_ibl = f_specular * sIntensity;

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
        vec3 lightIntensity = getLighIntensity(light, pointToLight);
        vec3 l_diffuse = lightIntensity * NdotL *  BRDF_lambertian(f0, f90, c_diff, specularWeight, VdotH);
        vec3 l_specular = lightIntensity * specularKSK(beckmannLUT, N, L, V, sRoughness);

        float shadow = 1 - pcfFilteredShadow(fs_in.lightSpacePosition);

        f_diffuse += shadow * l_diffuse;
        f_specular += shadow * l_specular * sIntensity;

        if(sssEnaled()) {
            vec3 albedo = (lightIntensity * baseColor.rgb);
            f_diffuse += albedo * SSSTransmittance(uniforms.translucency, uniforms.sssWidth, fs_in.position, N, pointToLight, u_shadowMap, uniforms.lightSpaceMatrix, 100);
            f_specular += albedo * SSSTransmittance(uniforms.translucency, uniforms.sssWidth, fs_in.position, N, pointToLight, u_shadowMap, uniforms.lightSpaceMatrix, 100);
        }
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
    float lod = min(roughness, float(u_MipCount - 1));
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

float fresnel(vec3 halfV, vec3 view, float f0) {
    float base = 1.0 - dot(view, halfV);
    float exponential = pow(base, 5.0);
    return exponential + f0 * (1.0 - exponential);
}


float specularKSK(sampler2D beckmannTex, vec3 normal, vec3 light, vec3 view, float roughness) {
    vec3 halfV = view + light;
    vec3 halfVn = normalize(halfV);
    float ndotl = max(dot(normal, light), 0.0);
    float ndoth = max(dot(normal, halfVn), 0.0);

    const float specularFresnel = uniforms.specularFresnel;
    float ph = pow(2.0 * texture(beckmannTex, vec2(ndoth, roughness), 0).r, 10);
    float f = mix(0.25, fresnel(halfVn, view, F0), specularFresnel);
    float ksk = max(ph * f / dot(halfV, halfV), 0.0);

    return ndotl * ksk;
}

vec3 getDiffuseLight(vec3 n) {
    return texture(env_irradiance, dirToUV(u_EnvRotation * n)).rgb;
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

float pcfFilteredShadow(vec4 lightSpacePos){
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if(projCoords.z > 1.0){
        return 0.0;
    }
    float shadow = 0.0f;
    float currentDepth = projCoords.z;
    vec2 texelSize = 1.0/textureSize(u_shadowMap, 0);
    for(int x = -1; x <= 1; x++){
        for(int y = -1; y <= 1; y++){
            float pcfDepth = texture(u_shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow/9.0;
}

vec3 computeBumpNormalScreenSpace(vec2 uv, float bumpScale) {
    vec3 bump = -1 + 2 * texture(bumpMap, uv).rgb;
    return mix(vec3(0,0,1), bump, bumpScale);
}

vec3 bumpToNormal(vec2 uv, float bumpScale) {
    const float bumpStrength = 10;
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

vec3 SSSTransmittance(float translucency, float sssWidth, vec3 worldPosition, vec3 worldNormal, vec3 light,
                        sampler2D shadowMap, mat4 lightViewProjection, float lightFarPlane) {

    /**
 * Calculate the scale of the effect.
 */
    float scale = 8.25 * (1.0 - translucency) / sssWidth;

    /**
     * First we shrink the position inwards the surface to avoid artifacts:
     * (Note that this can be done once for all the lights)
     */
    vec4 shrinkedPos = vec4(worldPosition - 0.005 * worldNormal, 1.0);

    /**
     * Now we calculate the thickness from the light point of view:
     */
    vec4 shadowPosition =  lightViewProjection * shrinkedPos;
    vec2 uv = (shadowPosition.xy * 0.5 / shadowPosition.w) + 0.5;

    float d1 = texture(shadowMap, uv).r; // 'd1' has a range of 0..1
    float d2 = shadowPosition.z; // 'd2' has a range of 0..'lightFarPlane'
    d1 = linearizeDepth1(d1); // So we scale 'd1' accordingly:
    float d = scale * abs(d1 - d2);

    /**
     * Armed with the thickness, we can now calculate the color by means of the
     * precalculated transmittance profile.
     * (It can be precomputed into a texture, for maximum performance):
     */
    float dd = -d * d;
    vec3 profile = vec3(0.233, 0.455, 0.649) * exp(dd / 0.0064) +
    vec3(0.1,   0.336, 0.344) * exp(dd / 0.0484) +
    vec3(0.118, 0.198, 0.0)   * exp(dd / 0.187)  +
    vec3(0.113, 0.007, 0.007) * exp(dd / 0.567)  +
    vec3(0.358, 0.004, 0.0)   * exp(dd / 1.99)   +
    vec3(0.078, 0.0,   0.0)   * exp(dd / 7.41);

    /**
     * Using the profile, we finally approximate the transmitted lighting from
     * the back of the object:
     */
    return profile * clamp(0.3 + dot(light, -worldNormal), 0, 1);
}

float linearizeDepth1(float z) {
    const float near = uniforms.lightNearPlane;
    const float far = uniforms.lightFarPlane;
    return (near * far) / (z * (far - near) - far);
}
