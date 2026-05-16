#version 460

#include "hash.glsl"
#include "shared.glsl"

#define ATMOSPHERE_UNIFORM_SET 3
#include "atmosphere/atm_uniforms.glsl"

layout(location = 0) in vec2 pos;

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 viewSpacePos;
    vec3 viewDirection;
    vec3 color;
    vec2 uv;
} v;

layout(location = 5) flat out int isVisible;
layout(location = 6) noperspective out vec3 distance;

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

     isVisible = FrustumCullingTest(triangleVertices) ? 1 : 0;

    // compute final vertex attributes
    VertexAttribute attrib = TessellateTriangle(
        triangleTexCoords,
        pos
    );

    v.worldPos = (globals.modelMatrix * attrib.position).xyz;
    v.viewDirection = atm.cameraPosition - v.worldPos;
    v.viewSpacePos = (globals.viewMatrix * vec4(v.worldPos, 1)).xyz;
    v.color = vec3(252, 197, 150) / 255.0f;
    v.uv = attrib.texCoord;

    distance = vec3(1100);
    gl_Position = globals.modelViewProjectionMatrix * attrib.position;
}