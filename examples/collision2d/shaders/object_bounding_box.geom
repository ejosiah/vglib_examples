#version 460

#include "shared.glsl"

layout(points) in;

layout(line_strip, max_vertices = 5) out;

layout(location = 0) in float iRadius[];
layout(location = 0) out vec4 oColor;

void main() {
    float radius = iRadius[0] * SQRT2;
    vec2 center = gl_in[0].gl_Position.xy;
    Bounds bounds = createBounds(center, radius);

    vec2 pos = bounds.min;
    vec2 dim = bounds.max - bounds.min;

    oColor = vec4(0, 1, 1, 1);
    gl_Position = global.projection * vec4(pos, 0, 1);
    EmitVertex();

    pos.x += dim.x;
    oColor = vec4(0, 1, 1, 1);
    gl_Position = global.projection * vec4(pos, 0, 1);
    EmitVertex();

    pos.y += dim.y;
    oColor = vec4(0, 1, 1, 1);
    gl_Position = global.projection * vec4(pos, 0, 1);
    EmitVertex();

    pos.x -= dim.x;
    oColor = vec4(0, 1, 1, 1);
    gl_Position = global.projection * vec4(pos, 0, 1);
    EmitVertex();

    pos.y -= dim.y;
    oColor = vec4(0, 1, 1, 1);
    gl_Position = global.projection * vec4(pos, 0, 1);
    EmitVertex();

    EndPrimitive();
}