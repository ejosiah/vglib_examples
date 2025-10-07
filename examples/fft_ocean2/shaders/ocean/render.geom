#version 460

#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#include "uniforms.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec2 uv;
} gs_in[];

layout(location = 0) out struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec2 uv;
} gs_out;

layout(location = 3) noperspective out vec3 oDistance;

void main() {
    float area;
    vec2 v[3];

    if(wireframeEnabled()){
        vec2 p0 = u.screenResolution * gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
        vec2 p1 = u.screenResolution * gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;
        vec2 p2 = u.screenResolution * gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;
        v = vec2[3](p2 - p1, p2 - p0, p1 - p0);
        area = abs(v[1].x * v[2].y - v[1].y * v[2].x);
    }

    for (int i = 0; i < 3; ++i) {
        gs_out.worldPos = gs_in[i].worldPos;
        gs_out.viewDirection = gs_in[i].viewDirection;
        gs_out.uv = gs_in[i].uv;

        oDistance = vec3(1100);
        if(wireframeEnabled()){
            oDistance = vec3(0);
            oDistance[i] = area * inversesqrt(dot(v[i], v[i]));
        }
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }

    EndPrimitive();
}