#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable

#define DIFFUSE_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.x)]
#define AMBIENT_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.y)]
#define NORMAL_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.z)]
#define ROUGHNESS_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.w)]

const vec3 globalAmbient = vec3(0.2);
const vec3 Light = vec3(1);

struct Mesh {
    mat4 model;
    mat4 model_inverse;
    int materialId;
};

struct Material {
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
};

layout(set = 0, binding = 0) buffer MeshData {
    Mesh meshes[];
};

layout(set = 0, binding = 1) buffer MaterialData {
    Material materials[];
};

layout(set = 1, binding = 10) uniform sampler2D gTextures[];


layout(location = 0) in struct {
    vec3 tanget;
    vec3 bitangent;
    vec3 color;
    vec3 pos;
    vec3 localPos;
    vec3 normal;
    vec3 eyes;
    vec3 lightPos;
    vec2 uv;
} fs_in;

layout(location = 10)  in flat uint drawId;

layout(location = 0) out vec4 fragColor;

float saturate(float x){
    return max(0, x);
}

void main(){
    vec3 T = normalize(dFdx(fs_in.localPos));
    vec3 B = normalize(cross(fs_in.normal, fs_in.localPos));
    vec3 N = normalize(fs_in.normal);

    mat3 TBN = mat3(T, B, N);
    vec3 Ns = texture(NORMAL_TEXTURE, fs_in.uv).xyz;
    N = 2 * Ns - 1;
    N = TBN * N;
    vec3 L = normalize(fs_in.lightPos - fs_in.pos);
    vec3 E = normalize(fs_in.eyes - fs_in.pos);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    vec3 albedo = texture(DIFFUSE_TEXTURE, fs_in.uv).rgb;
    vec3 specular = materials[meshes[drawId].materialId].specular;
    float shininess = materials[meshes[drawId].materialId].shininess;

    vec3 color = Light * (saturate(dot(L, N)) * albedo + saturate(pow(dot(H, N), shininess)) * specular);
    color += globalAmbient * albedo;

    fragColor.rgb = pow(color, vec3(0.454545));
}