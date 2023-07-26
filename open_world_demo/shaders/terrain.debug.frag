#version 450

#define PI 3.1415926535897932384626433832795
#define TWO_PI 6.283185307179586476925286766559

#include "quaternion.glsl"
#include "terrain_ubo.glsl"
#include "pbr/common.glsl"

layout(set = 0, binding = 1) uniform sampler2D heightMap;
layout(set = 0, binding = 3) uniform sampler2D randomTexture;

layout(set = 1, binding = 0) uniform sampler2DArray albedoMap;
layout(set = 1, binding = 1) uniform sampler2DArray metalicMap;
layout(set = 1, binding = 2) uniform sampler2DArray roughnessMap;
layout(set = 1, binding = 3) uniform sampler2DArray normalMap;
layout(set = 1, binding = 4) uniform sampler2DArray aoMap;
layout(set = 1, binding = 5) uniform sampler2DArray displacementMap;
layout(set = 1, binding = 6) uniform sampler2D groundMask;

layout(location = 0) in struct {
    vec3 worldPosition;
    vec3 normal;
    vec2 uv;
    vec2 patch_uv;
    vec3 color;
} fs_in;

layout(location = 5) noperspective in vec3 edgeDist;


layout(location = 0) out vec4 fragColor;

const float preventDivideByZero = 0.0001;

float remap(float x, float a, float b, float c, float d){
    float t = clamp((x - a)/(b - a), 0, 1);
    return mix(c, d, t);
}

vec3 checkerboard(){
    vec2 id = floor(numPatches * fs_in.uv);
    float c = step(1, mod(id.x + id.y, 2));
    return mix(vec3(0.5), vec3(1), c);
}

float hash12(vec2 p)
{
    vec3 p3  = fract(vec3(p.xyx) * .1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 rotate(float angle, vec2 point){
    mat2 rot = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));

    return rot * point;
}

vec2 getUV(){
    vec2 scaledUV = (fs_in.uv) * numPatches;
    vec2 tile = floor(scaledUV);
    vec2 uv = fract(scaledUV);

    float offset = texture(randomTexture, tile/numPatches).x;
    float angle = hash12(tile + offset) * TWO_PI;
    uv = rotate(angle, uv);

    return uv;
}

struct Material {
    vec3 albedo;
    vec3 normal;
    float metalness;
    float roughness;
    float ambientOcclusion;
    float displacement;
};

Material loadMaterial(vec3 uv){

    vec3 albedo = texture(albedoMap, uv).rgb;
    vec3 normal = 2 * texture(normalMap, uv).rgb - 1;
    float metalness = texture(metalicMap, uv).r;
    float roughness = texture(roughnessMap, uv).r;
    roughness = (invertRoughness == 1) ? 1 - roughness : roughness;
    float ao = texture(aoMap, uv).r;
    float displacement = texture(displacementMap, uv).x;

    Material material;
    material.albedo = albedo;
    material.normal = normal;
    material.metalness = metalness;
    material.roughness = roughness;
    material.ambientOcclusion = ao;
    material.displacement = displacement;

    return material;
}

vec3 blend(vec4 color1, float a1, vec4 color2, float a2){
    float depth = 0.2;
    float ma = max(color1.a + a1, color2.a + a2) - depth;

    float b1 = max(color1.a + a1 - ma, 0);
    float b2 = max(color2.a + a2 - ma, 0);

    return (color1.rgb * b1 + color2.rgb * b2)/(b1 + b2);
}

Material layeredMaterial(vec2 uv){
    Material grassMaterial = loadMaterial(vec3(uv, greenGrass));
    Material dirtMaterial = loadMaterial(vec3(uv, dirtRock));
    float t = texture(groundMask, fs_in.uv).r;

    Material groundMaterial;

    groundMaterial.albedo = mix(grassMaterial.albedo, dirtMaterial.albedo, t);
    groundMaterial.metalness = mix(grassMaterial.metalness, dirtMaterial.metalness, t);
    groundMaterial.roughness = mix(grassMaterial.roughness, dirtMaterial.roughness, t);
    groundMaterial.ambientOcclusion = mix(grassMaterial.ambientOcclusion, dirtMaterial.ambientOcclusion, t);
    groundMaterial.displacement = mix(grassMaterial.displacement, dirtMaterial.displacement, t);

    groundMaterial.normal = mix(grassMaterial.normal, dirtMaterial.normal, t);

    Material snowMaterial = loadMaterial(vec3(uv, snowFresh));

    Material layeredMat;
    vec4 color1 = vec4(groundMaterial.albedo, grassMaterial.displacement);
    vec4 color2 = vec4(snowMaterial.albedo, snowMaterial.displacement);

    float y = remap(fs_in.worldPosition.y, minZ, maxZ, 0, 1);
    float a1 = 1 - smoothstep(0, snowStart, y);
    float a2 = smoothstep(snowStart, 1, y);

    layeredMat.albedo = blend(color1, a1, color2, a2);

    color1.rgb = vec3(groundMaterial.metalness);
    color2.rgb = vec3(snowMaterial.metalness);
    layeredMat.metalness = blend(color1, a1, color2, a2).r;

    color1.rgb = vec3(groundMaterial.roughness);
    color2.rgb = vec3(snowMaterial.roughness);
    layeredMat.roughness = blend(color1, a1, color2, a2).r;

    color1.rgb = vec3(groundMaterial.ambientOcclusion);
    color2.rgb = vec3(snowMaterial.ambientOcclusion);
    layeredMat.ambientOcclusion = blend(color1, a1, color2, a2).r;

    color1.rgb = grassMaterial.normal;
    color2.rgb = snowMaterial.normal;
    layeredMat.normal = blend(color1, a1, color2, a2);

    return layeredMat;
}

vec3 Normal = vec3(0);

vec3 shadeFragment(){
    vec2 uv = getUV();

    Material material = layeredMaterial(uv);
    vec3 albedo = material.albedo;
    float metalness = material.metalness;
    float roughness = material.roughness;
    float ao = material.ambientOcclusion;

    vec3 n1 = fs_in.normal;
    vec3 n2 = material.normal;
    vec3 N = normalize(vec3(n1.xy*n2.z + n2.xy*n1.z, n1.z*n2.z));

    // rotate normal into world space
    vec4 q = axisAngle(vec3(1, 0, 0), -PI/2);
    N = rotatePoint(q, N);
    N = normalize(N);

    Normal = N;

    vec3 viewDir = cameraPosition - fs_in.worldPosition;
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
    vec3 color = vec3(0);
    if(shading == 1){
        color = shadeFragment();
    }else{
        vec3 N = normalize(heightScale <= 0 ? vec3(0, 0, 1) : fs_in.normal);
        vec3 albedo = checkerboard();
        vec4 q = axisAngle(vec3(1, 0, 0), -PI/2);
        N = rotatePoint(q, N);
        N = normalize(N);

        vec3 L = normalize(sunPosition);
        color = albedo * max(0, dot(N, L));
    }


    if(bool(wireframe)){
        float d = min(edgeDist.x, min(edgeDist.y, edgeDist.z));
        float t = smoothstep(-wireframeWidth, wireframeWidth, d);

        vec3 wcolor = lod == 1 && tessLevelColor == 1 ? fs_in.color : wireframeColor;
        color = mix(wcolor, color, t);
    }

    fragColor.rgb = color/(1 + color);
    fragColor.a = 1;
}