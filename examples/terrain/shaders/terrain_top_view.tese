#version 460

#include "shared.glsl"

layout (triangles, ccw, equal_spacing) in;

layout(location = 0) in struct {
    vec4 vertices[3];
} te_in[];

layout (location = 0) flat out int o_IsVisible;

void main() {
    vec4 v[3] = te_in[0].vertices;
    vec4 finalVertex = BarycentricInterpolation(v, gl_TessCoord.xy);

    gl_Position = vec4(finalVertex.xy * 2.0 - 1.0, 0.0, 1.0);
    o_IsVisible = v[0].w > 0.0 ? 1 : 0;
}