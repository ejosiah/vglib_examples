#version 460

#include "hash.glsl"
#include "shared.glsl"

layout(location = 0) in vec2 pos;

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 color;
    vec2 uv;
} v;

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

    v.worldPos = (globals.modelMatrix * attrib.position).xyz;
    v.color = vec3(252, 197, 150) / 255.0f;
    v.uv = attrib.texCoord;

    gl_Position = globals.modelViewProjectionMatrix * attrib.position;
}