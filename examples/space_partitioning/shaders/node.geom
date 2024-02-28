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

    if(pIndex == -1) return;

    Point p = points[pIndex];

    const int N = 128;
    float delta = TWO_PI/(N - 1);

    for(int i = 0; i < N; ++i) {
        float angle = delta * i;
        vec2 vertex = p.position + r * vec2(cos(angle), sin(angle));

        color = red;
        gl_Position = vec4(vertex, 0, 1);
        EmitVertex();
    }
}