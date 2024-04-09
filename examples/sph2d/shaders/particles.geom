#version 460

#include "shared.glsl"

layout(points) in;

layout(triangle_strip, max_vertices = 128) out;

layout(location = 0) in vec4 iColor[];
layout(location = 0) out vec4 oColor;

void main() {
    const int N = 12;
    float delta = (2 * PI)/(N - 1);
    float radius = particleRadius;
    vec2 center = gl_in[0].gl_Position.xy;
    vec4 color = iColor[0];
    for(int i = 0; i <= N; ++i) {

        if(i % 3 == 0) {
            gl_Position = projection * vec4(center, 0, 1);
            oColor = color;
            EmitVertex();
        }

        float angle = delta * i;
        vec2 vertex = center + radius * vec2(cos(angle), sin(angle));

        gl_Position = projection * vec4(vertex, 0, 1);
        oColor = color;
        EmitVertex();
    }
}