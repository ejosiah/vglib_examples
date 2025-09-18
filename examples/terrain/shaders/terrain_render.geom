#version 460

#include "shared.glsl"  // TODO move uniform out of shared

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec3 color;
    vec2 uv;
} gs_in[];

layout(location = 4) flat in int iIsVisible[];

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec3 color;
    vec2 uv;
} gs_out;

layout(location = 4) flat out int isVisible;

layout(location = 5) noperspective out vec3 o_Distance;

void main() {
    vec2 p0 = globals.resolution * gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
    vec2 p1 = globals.resolution * gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
    vec2 p2 = globals.resolution * gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    for (int i = 0; i < 3; ++i) {
        gs_out.worldPos = gs_in[i].worldPos;
        gs_out.viewDirection = gs_in[i].viewDirection;
        gs_out.color = gs_in[i].color;
        gs_out.uv = gs_in[i].uv;

        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        isVisible = iIsVisible[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }

    EndPrimitive();
}