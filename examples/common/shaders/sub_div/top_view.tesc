#version 460

#define CBT_HEAP_BUFFER_BINDING 0
#include "leb_common.glsl"

layout (vertices = 1) out;

layout(location = 0) out struct {
    vec4 vertices[3];
} tc_out[];

void main() {
    const int cbtID = 0;
    uint threadID = uint(gl_PrimitiveID);
    cbt_Node node = cbt_DecodeNode(cbtID, threadID);
    vec4 v[3] = DecodeTriangleVertices(node);

    // set tess levels
    gl_TessLevelInner[0] =
    gl_TessLevelInner[1] =
    gl_TessLevelOuter[0] =
    gl_TessLevelOuter[1] =
    gl_TessLevelOuter[2] = 1;

    // set output data
    tc_out[gl_InvocationID].vertices = v;
}