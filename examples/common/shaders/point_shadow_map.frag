#version 460

#extension GL_EXT_nonuniform_qualifier : enable

#define MATERIAL_ID meshes[nonuniformEXT(drawId)].materialId
#define MATERIAL materials[MATERIAL_ID]

layout(push_constant) uniform Camera {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 position;
    float farPlane;
} light;

//layout(set = 0, binding = 0) buffer MeshData {
//    mat4 model;
//    mat4 model_inverse;
//    int materialId;
//} meshes[];
//
//layout(set = 0, binding = 1) buffer MaterialData {
//    vec3 diffuse;
//    float shininess;
//
//    vec3 ambient;
//    float ior;
//
//    vec3 specular;
//    float opacity;
//
//    vec3 emission;
//    float illum;
//
//    uvec4 textures0;
//    uvec4 textures1;
//    vec3 transmittance;
//} materials[];

layout(location = 0) in struct {
    vec3 worldPos;
} fs_in;

layout(location = 1) in flat int drawId;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_FragDepth = distance(fs_in.worldPos, light.position)/light.farPlane;
    fragColor = vec4(0);
}