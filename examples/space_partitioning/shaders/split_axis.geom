#version 460

#include "domain.glsl"

layout(points) in;

layout(line_strip, max_vertices = 2) out;

layout(set = 0, binding = 0) buffer Points {
    Point points[];
};

layout(set = 0, binding = 1) buffer TreeIndex {
    int tree[];
};


layout(location = 0) in flat int iInstanceId[];
layout(location = 1) in vec3 iColor[];

layout(location = 1) out vec3 color;

void main() {
    int index = tree[iInstanceId[0]];

    if(index == -1) return;

    Point p = points[index];

    if(p.axis >= 0){

        color = vec3(1, 1, 0);
        gl_Position = vec4(p.start, 0, 1);
        EmitVertex();


        color = vec3(1, 1, 0);
        gl_Position = vec4(p.end, 0, 1);
        EmitVertex();

        EndPrimitive();
    }
}