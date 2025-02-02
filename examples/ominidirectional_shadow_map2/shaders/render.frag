#version 460

#define EPSILON 0.15
#define SHADOW_OPACITY 0.5

layout(set = 2, binding = 10) uniform samplerCube global_textures_cubemap[];

layout(location = 0) in struct {
    vec4 color;
    vec3 localPos;
    vec3 wordPos;
    vec3 viewPos;
    vec3 normal;
    vec3 tangent;
    vec3 bitangent;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv[2];
} fs_in;

layout(push_constant) uniform Constants {
    layout(offset=192)
    vec3 lightPosition;
};

layout(location = 13) in flat int drawId;

layout(location = 0) out vec4 fragColor;

const float Ambient = 0.05;

float sampleShadowMap(vec3 position) {
    return texture(global_textures_cubemap[0], position).r;
}

void main() {
    vec3 L = normalize(lightPosition - fs_in.wordPos);
    vec3 N = normalize(fs_in.normal);
    vec3 albedo = fs_in.color.rgb;

    float diffuse = max(0, dot(N, L));

    vec3 color = (Ambient + diffuse) * albedo;

    vec3 lightVector = fs_in.wordPos - lightPosition;
    float distanceToLight = length(lightVector);
    float sampleDistance = sampleShadowMap(lightVector);
    float visibility = (distanceToLight - sampleDistance) <= EPSILON ? 1 : SHADOW_OPACITY;

    color  *= visibility;

    fragColor = vec4(color, 1);
}