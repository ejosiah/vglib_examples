#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "scene.glsl"
#include "terrain_lod.glsl"

#define HEIGHT_FIELD global_textures[scene.height_field_texture_id]

layout(set = 2, binding = 10) uniform sampler2D global_textures[];

layout (vertices = 4) out;

layout(location = 0) in vec2 vs_uv[gl_MaxPatchVertices];
layout(location = 1) flat in int patchId[gl_MaxPatchVertices];

layout(location = 0)  out vec2 tc_uv[4];
layout(location = 1) flat out int tc_patchId[4];

void main(){

    if(gl_InvocationID == 0){
//        LodParams lodParams;
//
//        lodParams.lodType = LOD_TYPE_DISTANCE_FROM_CAMERA;
//        lodParams.modelView = scene.view * scene.model;
//
//        lodParams.positions[0] = gl_in[0].gl_Position;
//        lodParams.positions[1] = gl_in[1].gl_Position;
//        lodParams.positions[2] = gl_in[2].gl_Position;
//        lodParams.positions[3] = gl_in[3].gl_Position;
//
//        lodParams.displacement[0] = texture(HEIGHT_FIELD, vs_uv[0]).y * scene.amplitude;
//        lodParams.displacement[1] = texture(HEIGHT_FIELD, vs_uv[1]).y * scene.amplitude;
//        lodParams.displacement[2] = texture(HEIGHT_FIELD, vs_uv[2]).y * scene.amplitude;
//        lodParams.displacement[3] = texture(HEIGHT_FIELD, vs_uv[3]).y * scene.amplitude;
//
//        lodParams.minDepth = 5;
//        lodParams.maxDepth = 1000;
//
//        lodParams.minTessLevel = 8;
//        lodParams.maxTessLevel = 64;
//
//        terrainLOD(lodParams, gl_TessLevelOuter, gl_TessLevelInner);
        gl_TessLevelOuter[0] = 64;
        gl_TessLevelOuter[1] = 64;
        gl_TessLevelOuter[2] = 64;
        gl_TessLevelOuter[3] = 64;
        gl_TessLevelInner[0] = 64;
        gl_TessLevelInner[1] = 64;

    }
    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    tc_uv[gl_InvocationID] = vs_uv[gl_InvocationID];
    tc_patchId[gl_InvocationID] = patchId[gl_InvocationID];
}