#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "pbr/common.glsl"
#include "scene.glsl"

layout(set = 2, binding = 10) uniform sampler2D global_textures[];
layout(set = 2, binding = 10) uniform sampler3D global_textures3d[];

#define GRADIENT_MAP (global_textures[scene.gradient_texture_id])

#define RADIANCE_API_ENABLED
#define BIND_LESS_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#include "atmosphere_api.glsl"

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) flat in int patchId;

layout(location = 0) out vec4 fragColor;

const vec3 deepBlue = vec3(0.0056f, 0.0194f, 0.0331f);
const vec3 carribbean = vec3(0.1812f, 0.4678f, 0.5520f);
const vec3 lightBlue = vec3(0.0000f, 0.2307f, 0.3613f);

vec3 hash31(float p){
    vec3 p3 = fract(vec3(p) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xxy+p3.yzz)*p3.zyx);
}

//vec3 fresnelSchlick(float cosTheta, vec3 F0) {
//    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
//}

const float preventDivideByZero = 0.0001;

void main(){
    fragColor = vec4(1);

    vec3 F0 = vec3(0.02);

    vec4 grad = texture(GRADIENT_MAP, uv);

    vec3 viewDir = scene.camera - worldPos;

    vec3 N = normalize(grad.xzy);
//    vec3 N = normalize(normal);
    vec3 L = normalize(scene.sunDirection);
    vec3 E = normalize(viewDir);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-E, N);

    float roughness = 0;
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, E, L, roughness);

    vec3 F = fresnelSchlick(max(dot(E,H), 0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, E), 0.0) * max(dot(N, L), 0.0) + preventDivideByZero;

    vec3 specular = vec3(1);

    vec3 transmittance;
    vec3 reflection = GetSkyRadiance(scene.camera - scene.earthCenter, R, 0, scene.sunDirection, transmittance);
    if (dot(R, scene.sunDirection) > scene.sunSize.y) {
        reflection = reflection + transmittance * GetSolarRadiance();
    }

    vec3 kD = 1 - F;
    vec3 kS = F;
    float NdotL = max(dot(N, L), 0.0);

    vec3 sky_irradiance;
    vec3 sun_irradiance = GetSunAndSkyIrradiance(worldPos - scene.earthCenter, R, scene.sunDirection, sky_irradiance);
    vec3 ocean_radiance = (kD * carribbean / PI + specular * kS) * (sky_irradiance + sun_irradiance) * NdotL;

    vec3 in_scatter = GetSkyRadianceToPoint(scene.camera - scene.earthCenter, worldPos - scene.earthCenter, 0, scene.sunDirection, transmittance);
    ocean_radiance = ocean_radiance * transmittance + in_scatter;

    vec3 color = ocean_radiance;

//    color = hash31(float(patchId + 1));
    fragColor.rgb = pow(vec3(1.0) - exp(-color / scene.whitePoint * scene.exposure), vec3(1.0 / 2.2));
}