#version 460

#include "terrain_ubo.glsl"
#include "pbr/common.glsl"

layout(set = 1, binding = 0) uniform sampler2D gPosition;
layout(set = 1, binding = 1) uniform sampler2D gNormal;
layout(set = 1, binding = 2) uniform sampler2D gAlbedo;
layout(set = 1, binding = 3) uniform sampler2D gMaterial;
layout(set = 1, binding = 4) uniform sampler2D gEdgeDist;
layout(set = 1, binding = 5) uniform sampler2D gDepth;

layout(location = 0) in struct {
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

const float preventDivideByZero = 0.0001;

vec3 shadeFragment(){
    vec2 uv = fs_in.uv;

    vec4 material = texture(gMaterial, uv);
    vec3 albedo = texture(gAlbedo, uv).rgb;
    float metalness = material.r;
    float roughness = material.g;
    float ao = material.b;


    vec3 N = normalize(texture(gNormal, uv).rgb);

    vec3 worldPosition = texture(gPosition, uv).xyz;
    vec3 viewDir = cameraPosition - worldPosition;
    vec3 E = normalize(viewDir);
    vec3 R = reflect(-E, N);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metalness);

    vec3 lightDir = sunPosition;
    vec3 L = normalize(lightDir);

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

    vec3 Lo = (kD * albedo / PI + specular) * radiance * NdotL;

    return Lo;
}


void main(){
    vec2 uv = fs_in.uv;

    vec3 color = vec3(0);
    vec3 albedo = texture(gAlbedo, uv).rgb;
    vec3 normal = texture(gNormal, uv).xyz;

    if(shading == 1){
        color = shadeFragment();
    }else{
        vec3 N = normalize(heightScale <= 0 ? vec3(0, 1, 0) : normal);

        vec3 L = normalize(sunPosition);
        color = albedo * max(0, dot(N, L));
    }


    if(bool(wireframe)){
        vec3 edgeDist = texture(gEdgeDist, fs_in.uv).xyz;
        float d = min(edgeDist.x, min(edgeDist.y, edgeDist.z));
        float t = smoothstep(-wireframeWidth, wireframeWidth, d);

        vec3 wcolor = lod == 1 && tessLevelColor == 1 ? albedo : wireframeColor;
        color = mix(wcolor, color, t);
    }

    fragColor.rgb = color/(1 + color);
    fragColor.a = 1;

}