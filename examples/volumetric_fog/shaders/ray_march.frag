#version 460 core
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define DIFFUSE_TEXTURE_ID MATERIAL.textures0.x
#define METALNESS_TEXTURE_ID MATERIAL.textures0.y
#define SPECULAR_TEXTURE_ID MATERIAL.textures0.z
#define ROUGHNESS_TEXTURE_ID MATERIAL.textures0.w
//#define NORMAL_TEXTURE_ID MATERIALtextures1.x
#define MASK_TEXTURE_ID MATERIAL.textures1.y
#define NORMAL_TEXTURE_ID 0
#define BRDF_TEXTURE_ID 1
#define VOLUME_SCATTERING_TEXTURE_ID 7
#define BLUE_NOISE_TEXTURE_ID 8
#define VOLUME_NOISE_TEXTURE_ID 9

#define DIFFUSE_TEXTURE gTextures[nonuniformEXT(DIFFUSE_TEXTURE_ID)]
#define METALNESS_TEXTURE gTextures[nonuniformEXT(METALNESS_TEXTURE_ID)]
#define SPECULAR_TEXTURE gTextures[nonuniformEXT(SPECULAR_TEXTURE_ID)]
#define ROUGHNESS_TEXTURE gTextures[nonuniformEXT(ROUGHNESS_TEXTURE_ID)]
#define NORMAL_TEXTURE gTextures[nonuniformEXT(NORMAL_TEXTURE_ID)]
#define MASK_TEXTURE gTextures[nonuniformEXT(MASK_TEXTURE_ID)]
#define BLUE_NOISE_TEXTURE gTextures[BLUE_NOISE_TEXTURE_ID]
#define VOLUME_SCATTERING_TEXTURE gTextures3d[VOLUME_SCATTERING_TEXTURE_ID]
#define VOLUME_NOISE_TEXTURE gTextures3d[VOLUME_NOISE_TEXTURE_ID]


#define RADIANCE_API_ENABLED
#define SCENE_SET 3
#define FOG_SET 7
#define BIND_LESS_ENABLED

layout(set = 6, binding = 10) uniform sampler2D gTextures[];
layout(set = 6, binding = 10) uniform sampler3D gTextures3d[];

#include "atmosphere_api.glsl"
#include "scene.glsl"
#include "shadow_map.glsl"
#include "fog.glsl"

float RadicalInverse_VdC(uint bits){
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley(uint i, uint N){
    return vec2(float(i)/float(N), RadicalInverse_VdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness*roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

    // from spherical coordinates to cartesian coordinates
    vec3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // from tangent-space vector to world-space sample vector
    vec3 up        = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent   = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    vec3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
// ----------------------------------------------------------------------------
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = geometrySchlickGGX(NdotV, roughness);
    float ggx1 = geometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 hammersleyHemi(uint i, const uint N) {
    vec2 P = hammersley(i, N);
    float phi = P.y * 2.0 * PI;
    float cosTheta = 1.0 - P.x;
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness*roughness;
    float a2 = a*a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float fresnelSchlickScalar(float cosTheta, float F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

const vec3 globalAmbient = vec3(0.02);
const vec3 Light = vec3(1);
const float preventDivideByZero = 0.0001;

layout(constant_id = 0) const uint materialOffset = 192;

layout(set = 4, binding = 0) uniform LightSpaceMatrix {
    mat4 lightViewProjection;
};

layout(set = 2, binding = 0) buffer MeshData {
    mat4 model;
    mat4 model_inverse;
    int materialId;
} meshes[];

layout(set = 2, binding = 1) buffer MaterialData {
    vec3 diffuse;
    float shininess;

    vec3 ambient;
    float ior;

    vec3 specular;
    float opacity;

    vec3 emission;
    float illum;

    uvec4 textures0;
    uvec4 textures1;
    vec3 transmittance;
} materials[];


layout(location = 0) in struct {
    vec4 lightSpacePos;
    vec3 position;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 6) in flat int drawId;

layout(location = 0) out vec4 fragColor;

float saturate(float x){
    return max(0, x);
}

vec4 constantFog(float noise) {
    float density = fog.constantDensity * noise;
    return scatteringExtinctionFromColorDensity(FOG_COLOR, density);
}

vec4 heightFog(vec3 pos, float noise) {
    float h = max(0, pos.y);
    float density = fog.heightFogDensity * exp(-fog.heightFogFalloff * h) * noise;
    return scatteringExtinctionFromColorDensity(FOG_COLOR, density);
}

float compute(vec3 pos, float distance, out vec3 scattering, out float transmittance) {
    vec3 sampleCoord = pos + fog.volumeNoisePositionScale + vec3(1, 0.1, 2) * scene.time * fog.volumeNoiseSpeedScale;

    float noise = texture(VOLUME_NOISE_TEXTURE, sampleCoord).r;
    vec4 scatteringAndExtinction = constantFog(noise) + heightFog(pos, noise);
    scattering = scatteringAndExtinction.rgb;

    float extinction = scatteringAndExtinction.a;
    transmittance = exp(-extinction * distance);

    return extinction;
}

float computeInScattering(vec3 pos, inout vec3 totalScattering) {
    const vec3 sunLight = ATMOSPHERE.solar_irradiance/PI;
    const vec3 wi = normalize(pos - scene.camera);
    const int maxSteps = 50;
    float currentStep = 0;
    float maxDistance = length(pos - scene.camera);
    const int stepCount = maxSteps;
    float totalDistance = 0;
    float totalTransmittance = 1;
    float stepDelta = maxDistance/float(maxSteps);

    float d = stepDelta;
    for(int i = 0; i < stepCount; i++) {
        if(totalDistance > maxDistance) return totalTransmittance;

        vec3 lPos = scene.camera + wi * d;
        vec3 scattering;
        float transmittance;
        float extinction = compute(lPos, stepDelta, scattering, transmittance);
        scattering = (scattering - (scattering * transmittance))/ max(extinction, 0.00001);

        vec4 worldSpacePos = vec4(lPos, 1);
//        vec4 lightSpacePos = scene.lightViewProjection * worldSpacePos;
        vec4 lightSpacePos = lightViewProjection * worldSpacePos;
        vec3 L = normalize(scene.sunDirection);
        vec3 V = normalize(scene.camera - lPos);
        float visibility = 1 - shadowCalculation(lightSpacePos);
        float phaseFn = phaseHG(dot(-L, V), fog.g);
        totalScattering +=  scattering * totalTransmittance * visibility * phaseFn * sunLight;

        totalTransmittance *= transmittance;
        totalDistance = d;
        d += stepDelta;
    }

    return totalTransmittance;
}

vec4 getDiffuse() {
    if(DIFFUSE_TEXTURE_ID == 0){
        return vec4(MATERIAL.diffuse, 1);
    }
    return texture(DIFFUSE_TEXTURE, fs_in.uv);
}

float getMetalness() {
    if(METALNESS_TEXTURE_ID == 0){
        return 0;
    }
    return texture(METALNESS_TEXTURE, fs_in.uv).r;
}

float getRoughness() {
    if(ROUGHNESS_TEXTURE_ID == 0) {
        return 0;
    }
    return texture(ROUGHNESS_TEXTURE, fs_in.uv).r;
}

vec3 getNormal(){
    if(NORMAL_TEXTURE_ID == 0) {
        return normalize(fs_in.normal);
    }
    vec3 tangentNormal = texture(NORMAL_TEXTURE, fs_in.uv).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(fs_in.position);
    vec3 Q2  = dFdy(fs_in.position);
    vec2 st1 = dFdx(fs_in.uv);
    vec2 st2 = dFdy(fs_in.uv);

    vec3 N   = normalize(fs_in.normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

float getAmbientOcclusion() {
    return 1.0;
}

vec4 computeRadiance() {

    vec2 st = fs_in.uv;

    vec4 diffuse = getDiffuse();
    if(diffuse.a <= 0) return diffuse;

    vec3 albedo = diffuse.rgb;
    float metalness = getMetalness();
    float roughness = getRoughness();
    // roughness = bool(invertRoughness) ? 1 - roughness : roughness;
    vec3 normal = getNormal();

    vec3 viewDir = fs_in.eyes - fs_in.position;
    vec3 N = normalize(normal);
    vec3 E = normalize(viewDir);
    vec3 R = reflect(-E, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metalness);


    vec3 L = normalize(scene.sunDirection);
    vec3 H = normalize(E + L);

    vec3 skyIrradiance;
    vec3 radiance = GetSunAndSkyIrradiance(fs_in.position - scene.earthCenter, N, scene.sunDirection, skyIrradiance);

    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, E, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H,E), 0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, E), 0.0) * max(dot(N, L), 0.0) + preventDivideByZero;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metalness;

    // scale light by NdotL
    float NdotL = max(dot(N, L), 0.0);

    // add to outgoing radiance Lo
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    float ao = getAmbientOcclusion();
    vec3 ambient = globalAmbient * albedo * ao;
    Lo = ambient + Lo * (1 - shadowCalculation(fs_in.lightSpacePos));

    vec3 inScattering = vec3(0);
    float transmittance = computeInScattering(fs_in.position, inScattering);
    Lo = Lo * transmittance + inScattering;

    return vec4(Lo, 1);
}

void main(){
    vec4 color = computeRadiance();
    if(color.a <= 0) discard;

    fragColor.rgb = pow(vec3(1.0) - exp(-color.rgb / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
}