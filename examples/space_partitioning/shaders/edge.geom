#version 460

#include "domain.glsl"

#define TWO_PI 6.283185307179586476925286766559
#define QUATER_PI 0.78539816339744830961566084581988

layout(points) in;

layout(line_strip, max_vertices = 128) out;

layout(set = 0, binding = 0) buffer Points {
    Point points[];
};

layout(set = 0, binding = 1) buffer TreeIndex {
    int tree[];
};


layout(location = 0) in flat int iInstanceId[];
layout(location = 1) in vec3 iColor[];

layout(location = 1) out vec3 color;

const vec3 cyan = vec3(0, 1, 1);
const vec3 red = vec3(1, 0, 0);
const float r = 0.03;


void main() {

    int nodeIndex = iInstanceId[0];
    int pIndex = tree[nodeIndex];

    if(nodeIndex == 0 || pIndex < 0) return;

    int parentIndex = tree[(nodeIndex-1)/2];

    Point p1 = points[pIndex];
    Point p0 = points[parentIndex];

    vec2 dir = normalize(p1.position - p0.position);

    vec2 v0 = p0.position + r * dir;
    vec2 v1 = p1.position - r * dir;

    color = cyan;
    gl_Position = vec4(v0, 0, 1);
    EmitVertex();

    color = cyan;
    gl_Position = vec4(v1, 0, 1);
    EmitVertex();
}