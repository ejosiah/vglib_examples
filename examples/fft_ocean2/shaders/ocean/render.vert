#version 460

#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#include "subdivision.glsl"

layout(location = 0) in vec2 iPos;

layout(location = 0) out struct {
    vec4 color;
    vec3 worldPos;
    vec2 uv;
} vs_out;

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

    vec4 worldPos = u.modelMatrix * attrib.position;
    worldPos.xyz += sampleDisplacement(worldPos.xz);

    vs_out.worldPos = worldPos.xyz;
    vs_out.uv = attrib.texCoord;
    vs_out.color = vec4(1);

    gl_Position = u.viewProjectionMatrix * worldPos;
}