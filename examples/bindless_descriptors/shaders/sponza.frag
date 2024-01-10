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

}