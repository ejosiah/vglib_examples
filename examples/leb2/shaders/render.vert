#version 460

#define CBT_HEAP_BUFFER_BINDING 0
#include "leb_common.glsl"
#include "hash.glsl"

layout(location = 0) in vec2 iPos;

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

void main() {
    const int cbtID = 0;
    uint nodeID = gl_InstanceIndex;
    cbt_Node node = cbt_DecodeNode(cbtID, nodeID);

    vec4 triangleVertices[3] = DecodeTriangleVertices(node);

    vec2 triangleTexCoords[3] = vec2[3](
        triangleVertices[0].xy,
        triangleVertices[1].xy,
        triangleVertices[2].xy
    );

    VertexAttribute attrib = TessellateTriangle(triangleTexCoords, iPos);

    vColor = vec4(attrib.texCoord, 0, 1);
//    vColor.rgb = hash31(nodeID + 1);
    vUv = attrib.texCoord;
    vec2 pos = attrib.position.xy;
 //   pos.y *= -1;
    pos *= 2;
    pos -= 1;
    gl_Position = vec4(pos, 0, 1);
}