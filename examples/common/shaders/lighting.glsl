#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL
#define LIGHT_TYPE_POSITIONAL 1
#define LIGHT_TYPE_DIRECTIONAL 2

#include "brdf.glsl"

const vec3 defaultLightDir = vec3(0, 1, 1);
const vec3 whitePoint = vec3(1);
const float exposure = 10;

struct LightParams {
    vec3 position;
    vec3 viewDir;
    vec3 normal;
    vec3 albedo;
    float metalness;
    float roughness;
    float ambientOcclusion;
    vec3 lightDir;
    int lightType;
};

LightParams createLightParams(vec3 fragPos, vec3 viewPos, vec3 normal, vec3 lightPos, int lightType, vec3 albedo) {
    LightParams params;
    params.position = fragPos;
    params.viewDir = viewPos - fragPos;
    params.normal = normal;
    params.albedo = albedo;
    params.ambientOcclusion = 1;
    params.metalness = 0.2;
    params.roughness = 0.6;
    params.lightDir = lightType == LIGHT_TYPE_DIRECTIONAL ? normalize(lightPos) : lightPos - fragPos;
    params.lightType = lightType;

    return params;
}

const vec3 globalAmbient = vec3(0.02);
const float preventDivideByZero = 0.0001;

vec4 computeRadiance(LightParams params) {

    vec3 albedo = params.albedo;
    float metalness = params.metalness;
    float roughness = params.roughness;

    vec3 viewDir = params.viewDir;
    vec3 N = normalize(params.normal);
    vec3 E = normalize(viewDir);
    vec3 R = reflect(-E, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metalness);


    vec3 L = normalize(params.lightDir);
    vec3 H = normalize(E + L);

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
    vec3 radiance = vec3(1);
    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    float ao = params.ambientOcclusion;
    vec3 ambient = globalAmbient * albedo * ao;
    Lo = ambient + Lo; // * (1 - shadowCalculation(fs_in.lightSpacePos));

    return vec4(Lo, 1);
}

#endif // LIGHTING_GLSL