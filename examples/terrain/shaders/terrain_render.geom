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
layout(location = 5) noperspective in vec3 idistance[]; // unused

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec3 normal;
    vec3 color;
    vec2 uv;
} gs_out;

layout(location = 5) flat out int isVisible;

layout(location = 6) noperspective out vec3 o_Distance;

void main() {
    float area;
    vec2 v[3];

    float wArea = length(cross(gs_in[1].worldPos - gs_in[0].worldPos, gs_in[2].worldPos - gs_in[0].worldPos)) * 0.5;
    uint uArea = floatBitsToUint(wArea);
    globals.minArea = min(uArea, globals.minArea);

    if(wireframeEnabled()){
        vec2 p0 = globals.resolution * gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
        vec2 p1 = globals.resolution * gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
        vec2 p2 = globals.resolution * gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;
        v = vec2[3](p2 - p1, p2 - p0, p1 - p0);
        area = abs(v[1].x * v[2].y - v[1].y * v[2].x);
    }

    vec3 p0 = gs_in[0].worldPos;
    vec3 p1 = gs_in[1].worldPos;
    vec3 p2 = gs_in[2].worldPos;

    vec3 n = normalize(cross(p1 - p0, p2 - p0));

    for (int i = 0; i < 3; ++i) {
        gs_out.worldPos = gs_in[i].worldPos;
        gs_out.viewDirection = gs_in[i].viewDirection;
        gs_out.color = gs_in[i].color;
        gs_out.uv = gs_in[i].uv;
        gs_out.normal = n;

        o_Distance = vec3(1100);
        if(wireframeEnabled()){
            o_Distance = vec3(0);
            o_Distance[i] = area * inversesqrt(dot(v[i], v[i]));
        }
        isVisible = iIsVisible[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }

    EndPrimitive();
}