#version 460

#include "scene.glsl"

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

mat4 translation() {

    int numPatchX = int(sqrt(scene.numPatches));
    int numPatchY = numPatchX;

    int id = int(gl_InstanceIndex);
    float x = float(id % numPatchX) * scene.patchSize;
    float z =  float(id / numPatchX) * scene.patchSize;

    mat4 xform = mat4(1);
    xform[3][0] = x;
    xform[3][1] = 0;
    xform[3][2] = z;

    return xform;
}

layout(location = 0) out vec2 vUv;
layout(location = 1) flat out int patchId;

void main(){
    vUv = uv;
    patchId = gl_InstanceIndex;
    gl_Position = translation() * vec4(position.x, 0, position.y, 1);
}