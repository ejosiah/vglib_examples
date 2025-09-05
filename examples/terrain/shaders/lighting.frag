#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

#define RADIANCE_API_ENABLED
#define ATMOSPHERE_PARAMS_SET 2
#define ATMOSPHERE_LUT_SET 3
#include "atmosphere/bruneton_api.glsl"
#include "tone_mapping.glsl"

layout(set = 0, binding = 0, scalar) uniform Uniforms {
    mat4 inverseProjection;
    mat4 inverseView;

    vec3 sunDirection;
    uint gBufferColorIndex;

    vec3 cameraPos;
    uint gBufferPositionIndex;

    vec3 whitePoint;
    uint gBufferNormalIndex;

    vec2 resolution;
    vec2 sunSize;

    float exposure;
    uint gBufferDepthIndex;
    uint shadowMapIndex;
};

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

#define colors global_textures[nonuniformEXT(gBufferColorIndex)]
#define positions global_textures[nonuniformEXT(gBufferPositionIndex)]
#define normals global_textures[nonuniformEXT(gBufferNormalIndex)]
#define depth_buffer global_textures[nonuniformEXT(gBufferDepthIndex)]
#define shadow_map global_textures[nonuniformEXT(shadowMapIndex)]

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

vec3 tm_Uncharted2Tonemap(const vec3 x)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

vec3 tm_Uncharted2(const vec3 hdr)
{
    const float W = 11.2;
    const float exposureBias = 2.0;
    vec3 curr = tm_Uncharted2Tonemap(exposureBias * hdr);
    vec3 whiteScale = 1.0 / tm_Uncharted2Tonemap(vec3(W));
    return curr * whiteScale;
}

bool raySphere(in vec3 ro, in vec3 rd, in float radius, out float t0, out float t1)
{
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float disc = b*b - c;
    if (disc < 0.0) return false;
    float s = sqrt(disc);
    t0 = -b - s;
    t1 = -b + s;
    return t1 > 0.0;
}

void main(){
    vec4 clipPos = vec4(2 * uv - 1, 1, 1);
    vec4 viewPos = inverseProjection * clipPos;
    viewPos /= viewPos.w;
    vec3 cameraDir = normalize((inverseView * vec4(viewPos.xyz, 0)).xyz);
    vec3 earthCenter = atmosphereToLocalUnits(vec3(0, -ATMOSPHERE.bottom_radius, 0));

    vec3 radiance = vec3(0);
    vec3 camera = cameraPos - earthCenter;
    float shadow_length = 0;

    float depth = texture(depth_buffer, uv).r;
    vec3 debug = vec3(0);
    if(depth < 1){
        vec3 normal = -1 + 2 * texture(normals, uv).xzy;
        vec3 L = normalize(sunDirection);
        vec3 N = normalize(normal);
        vec4 color = texture(colors, uv);
        vec3 worldPos = texture(positions, uv).xyz;

        vec3 albedo = color.rgb;
        float visibility = color.a;

        vec3 point = worldPos - earthCenter;
        vec3 skyIrradiance;
        vec3 sunIrradiance = GetSunAndSkyIrradiance(point, N, L, skyIrradiance);
        radiance = (albedo / PI) * (skyIrradiance + sunIrradiance * visibility) ;

//        float shadow_length = length(worldPos - camera);
        vec3 transmittance;
        vec3 in_scatter = GetSkyRadianceToPoint(camera, point, shadow_length, sunDirection, transmittance);
        radiance = radiance * transmittance + in_scatter;

    } else {
        vec3 transmittance;


        radiance = GetSkyRadiance(camera , cameraDir, shadow_length, sunDirection, transmittance);
        if (dot(cameraDir, sunDirection) > sunSize.y) {
            radiance = radiance + transmittance * GetSolarRadiance();
        }
        vec3 point = camera + cameraDir * 100000;
        vec3 in_scatter = GetSkyRadianceToPoint(camera, point, shadow_length, sunDirection, transmittance);
        radiance = radiance * transmittance + in_scatter;
    }

    vec3 toneMapped = pow(vec3(1.0) - exp(-radiance / whitePoint * exposure), vec3(1.0 / 2.2));
//    vec3 toneMapped = tone_map(radiance, ACES);
//    toneMapped = pow(toneMapped, vec3(0.454));
    fragColor = vec4(toneMapped, 1);
}