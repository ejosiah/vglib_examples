#version 460

layout(constant_id = 0) const int screenResolutionX = 512;
layout(constant_id = 1) const int screenResolutionY = 512;

layout (triangles) in;
layout (points, max_vertices = 1) out;

layout(location = 0) flat in int heapIndex[];

layout(location = 0) flat out int oHeapIndex;

vec2 u_ScreenResolution = vec2(screenResolutionX, screenResolutionY);

void main() {
    vec2 p0 = gl_in[0].gl_Position.xy;
    vec2 p1 = gl_in[1].gl_Position.xy;
    vec2 p2 = gl_in[2].gl_Position.xy;

    vec2 p = (p0 + p1 + p2)/3;

    oHeapIndex = heapIndex[0];
    gl_PointSize = 32;
    gl_Position = vec4(p, 1, 1);

    EmitVertex();
    EndPrimitive();
}