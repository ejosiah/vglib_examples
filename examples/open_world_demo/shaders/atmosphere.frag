#version 460

#include "pbr/common.glsl"

#define RADIANCE_API_ENABLED

const float kLengthUnitInMeters = 1000.0;

layout(set = 0, binding = 0) uniform sampler2D irradiance_texture;
layout(set = 0, binding = 1) uniform sampler2D transmittance_texture;
layout(set = 0, binding = 2) uniform sampler3D scattering_texture;
layout(set = 0, binding = 3) uniform sampler3D single_mie_scattering_texture;

#include "atmosphere_api.h"

layout(set = 1, binding = 0) uniform UBO{
    mat4 viewToWorldSpaceMatrix;
    mat4 clipToViewSpaceMatrix;
    vec3 camera;
    vec3 white_point;
    vec3 earth_center;
    vec3 sun_direction;
    vec3 sun_size;
    float exposure;
    int light_shaft;
};

#ifdef USE_LUMINANCE
#define GetSolarRadiance GetSolarLuminance
#define GetSkyRadiance GetSkyLuminance
#define GetSkyRadianceToPoint GetSkyLuminanceToPoint
#define GetSunAndSkyIrradiance GetSunAndSkyIlluminance
#endif

vec3 GetSolarRadiance();

vec3 GetSkyRadiance(vec3 camera, vec3 view_ray, float shadow_length, vec3 sun_direction, out vec3 transmittance);

vec3 GetSkyRadianceToPoint(vec3 camera, vec3 point, float shadow_length, vec3 sun_direction, out vec3 transmittance);

vec3 GetSunAndSkyIrradiance(vec3 p, vec3 normal, vec3 sun_direction, out vec3 sky_irradiance);

float GetSunVisibility(vec3 normal, vec3 sun_direction) {
    return max(0, dot(normal, sun_direction));
}

float GetSkyVisibility(vec3 point) {
    return 0.0;
}

//layout(set = 2, binding = 0) uniform sampler2D gPosition;
layout(set = 2, binding = 1) uniform sampler2D gNormal;
layout(set = 2, binding = 2) uniform sampler2D gAlbedo;
layout(set = 2, binding = 3) uniform sampler2D gMaterial;
layout(set = 2, binding = 4) uniform sampler2D gDepth;
layout(set = 2, binding = 5) uniform usampler2D object_type_map;

layout(set = 3, binding = 0) uniform sampler2D shadowVolume;

void GetTerrainShadowInOut(vec2 uv, out float d_in, out float d_out) {
    if(light_shaft != 1){
        d_in = d_out = 0;
        return;
    }
    d_in = texture(shadowVolume, uv).x;
    d_out = texture(shadowVolume, uv).y;
}

layout(location = 0) in struct {
    vec3 view_ray;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

const float preventDivideByZero = 0.0001;

vec3 shadeFragment(vec3 light, vec3 worldPosition){
    vec2 uv = fs_in.uv;

    vec4 material = texture(gMaterial, uv);
    vec3 albedo = texture(gAlbedo, uv).rgb;
    float metalness = material.r;
    float roughness = material.g;
    float ao = material.b;


    vec3 N = normalize(texture(gNormal, uv).rgb);

//    vec3 worldPosition = texture(gPosition, uv).xyz;
    vec3 viewDir = camera - worldPosition;
    vec3 E = normalize(viewDir);
    vec3 R = reflect(-E, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metalness);

    vec3 L = sun_direction;

    vec3 H = normalize(E + L);
    float attenuation = 1;  // no attenuation for sun light
    vec3 radiance = vec3(10) * attenuation;

    // Cook-Torrance BRDF
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, E, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H,E), 0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, E), 0.0) * max(dot(N, L), 0.0) + preventDivideByZero;
    vec3 specular = numerator / denominator;

    vec3 kS = F;

    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metalness;

    float NdotL = max(dot(N, L), 0.0);

    vec3 Lo = light * (kD * albedo / PI + specular) * radiance * NdotL;

    return Lo;
}

const vec3 kGroundAlbedo = vec3(1, 0, 0);

vec3 getWorldPosition(vec2 uv, float depth){
    uv = 2 * uv - 1;
    vec4 clipSpacePosition = vec4(uv, depth, 1);
    vec4 viewSpacePosition = clipToViewSpaceMatrix * clipSpacePosition;
    viewSpacePosition /= viewSpacePosition.w;

    vec4 worldSpacePosition = viewToWorldSpaceMatrix * viewSpacePosition;

    return worldSpacePosition.xyz;
}

void main(){
    fragColor= vec4(0, 0, 0, 1);

    vec2 uv = fs_in.uv;
    vec3 view_ray = fs_in.view_ray;
    vec3 view_direction = normalize(view_ray);
    float fragment_angular_size = length(dFdx(view_ray) + dFdy(view_ray)) / length(view_ray);

    float depth = texture(gDepth, uv).x;

    float shadow_in = 0;
    float shadow_out = 0;
//    GetTerrainShadowInOut(uv, shadow_in, shadow_out);
    vec3 terrain_radiance = vec3(0);
    float terrain_alpha = 0;
    float lightshaft_fadein_hack = smoothstep(0.02, 0.04, dot(normalize(camera - earth_center), sun_direction));

    if(depth < 1){
        uint object_type = texture(object_type_map, uv).r;
        terrain_alpha = 1;
        //        vec3 point = texture(gPosition, uv).xyz;
        vec3 point = getWorldPosition(uv, depth)/1000;
        vec3 normal = texture(gNormal, uv).xyz;
        vec3 sky_irradiance;
        vec3 sun_irradiance = GetSunAndSkyIrradiance(point - earth_center, normal, sun_direction, sky_irradiance);
        vec3 albedo = texture(gAlbedo, uv).rgb;
        terrain_radiance = albedo * (1/PI) * (sun_irradiance * GetSunVisibility(normal, sun_direction));
        //        terrain_radiance = shadeFragment((sun_irradiance + sky_irradiance)/PI);
        float distance_to_intersection = distance(camera, point);
        float shadow_length = max(0.0, min(shadow_out, distance_to_intersection) - shadow_in) * lightshaft_fadein_hack;

        vec3 transmittance;
        vec3 in_scatter = GetSkyRadianceToPoint(camera - earth_center, point - earth_center, shadow_length, sun_direction, transmittance);
        terrain_radiance = terrain_radiance * transmittance + in_scatter;

    }


    vec3 transmittance;
    float shadow_length = max(0.0, shadow_out - shadow_in) * lightshaft_fadein_hack;
    vec3 radiance = GetSkyRadiance(camera - earth_center, view_direction, shadow_length, sun_direction, transmittance);
    if (dot(view_direction, sun_direction) > sun_size.y) {
        radiance = radiance + transmittance * GetSolarRadiance();
    }

    radiance = mix(radiance, terrain_radiance, terrain_alpha);
    radiance = texture(gAlbedo, uv).rgb;
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance / white_point * exposure), vec3(1.0 / 2.2));
}