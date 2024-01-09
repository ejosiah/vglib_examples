#version 460 core

#extension GL_EXT_nonuniform_qualifier : enable

#define ALBEDO_TEXTURE gTextures[nonuniformEXT(meshes[drawId].textures0.x)]
#define METALNESS_TEXTURE gTextures[nonuniformEXT(meshes[drawId].textures0.y)]
#define NORMAL_TEXTURE gTextures[nonuniformEXT(meshes[drawId].textures0.w)]
#define ROUGHNESS_TEXTURE gTextures[nonuniformEXT(meshes[drawId].textures1.z)]

const vec3 globalAmbient = vec3(0.2);
const vec3 Light = vec3(1);

struct Mesh {
    mat4 model;
    mat4 model_inverse;

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

layout(set = 1, binding = 10) uniform sampler2D gTextures[];


layout(location = 0) in struct {
    mat3 tbn;
    vec3 color;
    vec3 pos;
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