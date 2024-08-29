#version 460

#include "scene.glsl"

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

mat4 translatePatch() {
    int numPatchX = int(sqrt(scene.numPatches));
    int numPatchY =  numPatchX;
    float patchSize =  float(scene.tileSize) / float(numPatchX);

    int id = int(gl_InstanceIndex % scene.numPatches);
    float x = float(id % numPatchX) * patchSize;
    float z = float(id / numPatchX) * patchSize;

    mat4 xform = mat4(1);
    xform[3][0] = x;
    xform[3][1] = 0;
    xform[3][2] = z;

    return xform;
}

mat4 translateTile(int tileId) {

    int numTileX = int(sqrt(scene.numTiles));
    int numTileY = numTileX;

    int id = tileId;
    float x = float(id % numTileX) * scene.tileSize;
    float z =  float(id / numTileX) * scene.tileSize;

    mat4 xform = mat4(1);
    xform[3][0] = x;
    xform[3][1] = 0;
    xform[3][2] = z;

    return xform;
}

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out int patchId;

void main(){
    int tileId =  gl_InstanceIndex/scene.numPatches;

    int thisPatchid = int(gl_InstanceIndex % scene.numPatches);
    vec2 thisPatch;
    int numTileX = int(sqrt(scene.numTiles));
    int numPatchX = int(sqrt(scene.numPatches));
    thisPatch.x = float(thisPatchid % numPatchX);
    thisPatch.y =  float(thisPatchid / numPatchX);
//    vUv =  tile * uv;

    vUv =  (uv + thisPatch)/numPatchX;
    patchId = thisPatchid;
    gl_Position = translateTile(tileId) * translatePatch() * vec4(position.x, 0, position.y, 1);
}