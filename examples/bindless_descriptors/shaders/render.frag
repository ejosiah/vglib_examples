#version 460 core
#extension GL_EXT_scalar_block_layout : enable

const vec3 globalAmbient = vec3(0);
const vec3 Light = vec3(1);

layout(constant_id = 0) const uint materialOffset = 192;

layout(set = 0, binding = 0) uniform MATERIAL {
    vec3 diffuse;
    vec3 ambient;
    vec3 specular;
    vec3 emission;
    vec3 transmittance;
    float shininess;
    float ior;
    float opacity;
    float illum;
};

layout(set = 0, binding = 1) uniform sampler2D ambientMap;
layout(set = 0, binding = 2) uniform sampler2D diffuseMap;
layout(set = 0, binding = 3) uniform sampler2D specularMap;
layout(set = 0, binding = 4) uniform sampler2D normalMap;
layout(set = 0, binding = 5) uniform sampler2D ambientOcclusionMap;

layout(location = 0) in struct {
    vec3 color;
    vec3 pos;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

float saturate(float x){
    return max(0, x);
}

void main(){
    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(fs_in.lightPos - fs_in.pos);
    vec3 E = normalize(fs_in.eyes - fs_in.pos);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    vec3 albedo = texture(diffuseMap, fs_in.uv).rgb;
    vec3 color = Light * (saturate(dot(L, N)) * albedo + saturate(pow(dot(H, N), shininess)) * specular);
    fragColor = vec4(color, 1);

    fragColor.rgb = pow(fragColor.rgb, vec3(0.454545));
}