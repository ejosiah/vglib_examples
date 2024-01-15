#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL materials[meshes[drawId].materialId]

#define DIFFUSE_TEXTURE_ID materials[meshes[drawId].materialId].textures0.x
#define AMBIENT_TEXTURE_ID materials[meshes[drawId].materialId].textures0.y
#define SPECULAR_TEXTURE_ID materials[meshes[drawId].materialId].textures0.z
#define SHININESS_TEXTURE_ID materials[meshes[drawId].materialId].textures0.w
#define NORMAL_TEXTURE_ID materials[meshes[drawId].materialId].textures1.x

#define DIFFUSE_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.x)]
#define AMBIENT_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.y)]
#define SPECULAR_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.z)]
#define SHININESS_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures0.w)]
#define NORMAL_TEXTURE gTextures[nonuniformEXT(materials[meshes[drawId].materialId].textures1.x)]

const vec3 globalAmbient = vec3(0.2);
const vec3 Light = vec3(1);

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
    mat4 model;
    mat4 model_inverse;
    int materialId;
} meshes[];

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

vec3 getDiffuse() {
    if(DIFFUSE_TEXTURE_ID == 0){
        return MATERIAL.diffuse;
    }
    return texture(DIFFUSE_TEXTURE, fs_in.uv).rgb;
}

vec3 getSpecular() {
    if(SPECULAR_TEXTURE_ID == 0){
        return MATERIAL.specular;
    }
    return texture(SPECULAR_TEXTURE, fs_in.uv).rgb;
}

float getShininess() {
    if(SHININESS_TEXTURE_ID == 0) {
        return MATERIAL.shininess;
    }
    return texture(SHININESS_TEXTURE, fs_in.uv).r;
}

vec3 getNormal(){
    if(NORMAL_TEXTURE_ID == 0) {
        return normalize(fs_in.normal);
    }
    vec3 tangentNormal = texture(NORMAL_TEXTURE, fs_in.uv).xyz * 2.0 - 1.0;

    vec3 Q1  = dFdx(fs_in.pos);
    vec3 Q2  = dFdy(fs_in.pos);
    vec2 st1 = dFdx(fs_in.uv);
    vec2 st2 = dFdy(fs_in.uv);

    vec3 N   = normalize(fs_in.normal);
    vec3 T  = normalize(Q1*st2.t - Q2*st1.t);
    vec3 B  = -normalize(cross(N, T));
    mat3 TBN = mat3(T, B, N);

    return normalize(TBN * tangentNormal);
}

void main(){
    vec3 N = getNormal();

    vec3 L = normalize(fs_in.lightPos - fs_in.pos);
    vec3 E = normalize(fs_in.eyes - fs_in.pos);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    vec3 albedo = getDiffuse();
    vec3 specular = getSpecular();
    float shininess = getShininess();

    vec3 color = Light * (saturate(dot(L, N)) * albedo + saturate(pow(dot(H, N), shininess)) * specular);
    color += globalAmbient * albedo;

    fragColor.rgb = pow(color, vec3(0.454545));
}