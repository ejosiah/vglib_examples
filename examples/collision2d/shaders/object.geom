#version 460

#include "shared.glsl"

layout(points) in;

layout(triangle_strip, max_vertices = 128) out;

layout(location = 0) in float iRadius[];
layout(location = 1) in vec4 iColors[];

layout(location = 0) out vec4 oColor;

void main() {
    const int N = 42;
    float delta = (2 * PI)/(N - 1);
    float radius = iRadius[0];
    vec2 center = gl_in[0].gl_Position.xy;
    for(int i = 0; i <= N; ++i) {

        if(i % 3 == 0) {
            oColor = iColors[0];
            gl_Position = global.projection * vec4(center, 0, 1);
            EmitVertex();
        }

        float angle = delta * i;
        vec2 vertex = center + radius * vec2(cos(angle), sin(angle));

        oColor = iColors[0];
        gl_Position = global.projection * vec4(vertex, 0, 1);
        EmitVertex();
    }
}