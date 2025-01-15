#version 460

#extension GL_GOOGLE_include_directive : enable

#define CBT_HEAP_BUFFER_BINDING 0
#include "cbt.glsl"
#include "leb.glsl"

#define MODE_TRIANGLE 0
#define MODE_SQUARE 1

layout(constant_id = 0) const int mode = MODE_TRIANGLE;

void main() {
    cbt_Node node = cbt_DecodeNode(0, gl_InstanceIndex);
    vec3 xPos = vec3(0, 0, 1);
    vec3 yPos = vec3(1, 0, 0);

    mat2x3 posMatrix;
    if(mode == MODE_SQUARE){
        posMatrix = leb_DecodeNodeAttributeArray_Square(node, mat2x3(xPos, yPos));
    }else if(mode == MODE_TRIANGLE) {
        posMatrix = leb_DecodeNodeAttributeArray       (node, mat2x3(xPos, yPos));
    }

    vec2 pos = 2 * vec2(posMatrix[0][gl_VertexIndex], posMatrix[1][gl_VertexIndex]) - 1;
    pos.y *= -1;
    gl_Position = vec4(pos, 0.0, 1.0);
}