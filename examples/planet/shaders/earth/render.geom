#version 460

#include "../transforms.glsl"

#define GLOBAL_CB_SET 0
#include "../shared_lib/constant_buffers.glsl"

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in struct {
    vec3 positionRWS;
    vec3 positionORWS;
} gs_in[];

layout(location = 0) out struct {
    vec3 positionRWS;
    vec3 positionORWS;
    vec3 normal;
} gs_out;

layout(location = 3) noperspective out vec3 dist;

void main() {
    // Compute the vectors and the area
    vec2 p0 = clip_space_to_pixel(gl_in[0].gl_Position, _ScreenSize.xy);
    vec2 p1 = clip_space_to_pixel(gl_in[1].gl_Position, _ScreenSize.xy);
    vec2 p2 = clip_space_to_pixel(gl_in[2].gl_Position, _ScreenSize.xy);
    vec2 v[3] = { p2 - p1, p2 - p0, p1 - p0 };
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    vec3 e0 = gl_in[1].gl_Position.xyz - gl_in[0].gl_Position.xyz;
    vec3 e1 = gl_in[2].gl_Position.xyz - gl_in[0].gl_Position.xyz;
    vec3 normal = cross(e0, e1);

    // Emit the vertices
    for (uint vertID = 0; vertID < 3; ++vertID)
    {
        gl_Position = gl_in[vertID].gl_Position;
        gs_out.positionRWS = gs_in[vertID].positionRWS;
        gs_out.positionORWS = gs_in[vertID].positionORWS;
        gs_out.normal = normal;
        dist = vec3(0.0);
        dist[vertID] = area * inversesqrt(dot(v[vertID], v[vertID]));
        EmitVertex();
    }

    EndPrimitive();

}