#version 460

#include "shared.glsl"

layout(location = 0) in vec2 pos;

layout(location = 0) out vec4 wp;

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

    // compute final vertex attributes
    VertexAttribute attrib = TessellateTriangle(
        triangleTexCoords,
        pos
    );

    wp.xyz = (globals.modelMatrix * attrib.position).xyz;
    wp.w = FrustumCullingTest(triangleVertices) ? 1 : 0;
    gl_Position = globals.modelViewProjectionMatrix * attrib.position;
}