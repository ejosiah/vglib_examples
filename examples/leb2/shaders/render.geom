#version 460

layout (triangles) in;
layout (triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec4 iColor[];
layout(location = 1) in vec2 iUv[];

layout(location = 0) out vec4 oColor;
layout(location = 1) out vec2 oUv;

void main() {
    for(int i = 0; i < 3; ++i) {
        oColor = iColor[i];
        oUv = iUv[i];
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}