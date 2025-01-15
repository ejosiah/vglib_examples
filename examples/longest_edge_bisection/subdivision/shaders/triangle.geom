#version 460

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;
layout (location = 0) noperspective out vec3 o_Distance;


layout(constant_id = 0) const int screenResolutionX = 512;
layout(constant_id = 1) const int screenResolutionY = 512;

vec2 u_ScreenResolution = vec2(screenResolutionX, screenResolutionY);

void main() {
    vec2 p0 = u_ScreenResolution * gl_in[0].gl_Position.xy;
    vec2 p1 = u_ScreenResolution * gl_in[1].gl_Position.xy;
    vec2 p2 = u_ScreenResolution * gl_in[2].gl_Position.xy;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    for (int i = 0; i < 3; ++i) {
        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}