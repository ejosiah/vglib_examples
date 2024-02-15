#version 460

#include "domain.glsl"

layout(points) in;

layout(line_strip, max_vertices = 2) out;

layout(set = 0, binding = 0) buffer Points {
    Point points[];
};

layout(location = 0) in flat int iInstanceId[];
layout(location = 1) in vec3 iColor[];

layout(location = 1) out vec3 color;

void main() {
   Point p = points[iInstanceId[0]];

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