#version 460

#include "lighting.glsl"

layout(set = 0, binding = 0) uniform sampler2DArray albedoMap;
layout(set = 0, binding = 1) uniform sampler2D normalMap;
layout(set = 0, binding = 2) uniform sampler2D metalnessMap;
layout(set = 0, binding = 3) uniform sampler2D roughnessMap;
layout(set = 0, binding = 4) uniform sampler2D aoMap;

layout(push_constant) uniform ColorVariation {
layout(offset=192)
    int colorId;
};

layout(location = 0) in struct {
    vec4 color;
    vec3 worldPos;
    vec3 viewPos;
    vec3 normal;
    vec3 tanget;
    vec3 bitangent;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main() {
    mat3 TBN = mat3(normalize(fs_in.tanget), normalize(fs_in.bitangent), normalize(fs_in.normal));
    vec3 N = 2 * texture(normalMap, fs_in.uv).xyz - 1;
    N = TBN * N;

    if(!gl_FrontFacing) {
        N *= -1;
    }

    LightParams params;
    params.position = fs_in.worldPos;
    params.viewDir = fs_in.viewPos - fs_in.worldPos;
    params.normal = N;
    params.albedo =  gl_FrontFacing ? texture(albedoMap, vec3(fs_in.uv, colorId)).rgb : texture(albedoMap, vec3(fs_in.uv, 1)).rgb;
    params.metalness = texture(metalnessMap, fs_in.uv).r;
    params.roughness = texture(roughnessMap, fs_in.uv).r;
    params.ambientOcclusion = texture(aoMap, fs_in.uv).r;
    params.lightDir = defaultLightDir;
    params.lightType = LIGHT_TYPE_DIRECTIONAL;

    vec4 radiance = computeRadiance(params);
    fragColor.rgb = pow(vec3(1.0) - exp(-radiance.rgb / whitePoint * exposure), vec3(1.0 / 2.2));
}