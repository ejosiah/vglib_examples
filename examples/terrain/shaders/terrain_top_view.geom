#version 460

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;
layout (location = 0) flat in int i_IsVisible[];
layout (location = 0) flat out int o_IsVisible;
layout (location = 1) noperspective out vec3 o_Distance;

void main() {
    vec2 p0 = 800.0 * gl_in[0].gl_Position.xy;
    vec2 p1 = 800.0 * gl_in[1].gl_Position.xy;
    vec2 p2 = 800.0 * gl_in[2].gl_Position.xy;
    vec2 v[3] = vec2[3](p2 - p1, p2 - p0, p1 - p0);
    float area = abs(v[1].x * v[2].y - v[1].y * v[2].x);

    o_IsVisible = i_IsVisible[0];

    for (int i = 0; i < 3; ++i) {
        o_Distance = vec3(0);
        o_Distance[i] = area * inversesqrt(dot(v[i],v[i]));
        gl_Position = gl_in[i].gl_Position;
        gl_Position.y *= -1;
        EmitVertex();
    }
    EndPrimitive();
}