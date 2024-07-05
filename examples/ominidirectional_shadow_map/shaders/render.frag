#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

#define DIFFUSE_TEXTURE_ID MATERIAL.textures0.x
#define DIFFUSE_TEXTURE globalTextures[nonuniformEXT(DIFFUSE_TEXTURE_ID)]

layout(constant_id = 0) const uint DEPTH_SHADOW_MAP_ID = 0;
layout(constant_id = 1) const uint TRANSMISSION_SHADOW_MAP_ID = 1;

layout(set = 0, binding = 0) buffer MeshData {
    mat4 model;
    mat4 model_inverse;
    int materialId;
} meshes[];

layout(set = 0, binding = 1) buffer MaterialData {
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
} materials[];

layout(set = 1, binding = 10) uniform sampler2D globalTextures[];
layout(set = 1, binding = 10) uniform samplerCube globalTexturesCube[];

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec2 uv;
} fs_in;

layout(location = 3) in flat int drawId;

layout(location = 0) out vec4 fragColor;

const  vec3 lightPos = vec3(0.3837800026, 2.4106299877, 2.8933501244);
const float farPlane = 1000;

vec4 getDiffuse();

vec3 getEmission();

float shadowCalculation(vec3 fragPos);

float pcfFilteredShadow(vec3 fragPos);

void main() {
    vec3 lightDir = lightPos - fs_in.position;
    vec3 L = normalize(lightDir);
    vec3 N = normalize(fs_in.normal);

    if(!gl_FrontFacing){
        N *= -1;
    }

    float sqrDist = length(lightDir);
    sqrDist *= sqrDist;
    float radiance = max(0, dot(N, L))/sqrDist;

    vec4 albedo = getDiffuse();
    vec3 globalAmb = albedo.rgb * 0.2;

    lightDir.x *= -1;
    radiance += max(0, dot(fs_in.normal, lightDir));

    vec3 col = MATERIAL.emission + albedo.rgb * radiance;
    col = globalAmb + col * (1 - shadowCalculation(fs_in.position));


    col /= col + 1;
    col = pow(col, vec3(0.4545));
    fragColor.rgb = col;
}

vec4 getDiffuse() {
    if(DIFFUSE_TEXTURE_ID == 0){
        return vec4(MATERIAL.diffuse, 1);
    }
    return texture(DIFFUSE_TEXTURE, fs_in.uv);
}

float shadowCalculation(vec3 fragPos) {
    vec3 lightDir = fragPos - lightPos;
    float closestDepth = texture(globalTexturesCube[DEPTH_SHADOW_MAP_ID], lightDir).r * farPlane;
    float currentDepth = length(lightDir);
    float bias = 0.05;
    float shadow = (currentDepth - bias) > closestDepth ? 1 : 0;
    if(shadow == 1){
        return texture(globalTexturesCube[1], lightDir).a;
    }else {
        return 0;
    }
}
