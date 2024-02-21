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

vec2 circle[20];

void main() {

    int nodeIndex = iInstanceId[0];
    int pIndex = tree[nodeIndex];

    if(pIndex == -1) return;

    const int N = circle.length();
    float delta = TWO_PI/(N - 1);
    for(int i = 0; i < N; ++i) {
        float angle = delta * i;
        circle[i] = r * vec2(cos(angle), sin(angle));
    }

    Point p = points[pIndex];

    if(p.axis >= 0){

        for(int i = 0; i < N; ++i){
            vec2 vertex = p.position + circle[i];
            color = red;
            gl_Position = vec4(vertex, 0, 1);
            EmitVertex();
        }

        int leftChild = tree[2 * nodeIndex + 1];
        if(leftChild != -1){
            color = cyan;
            float angle = -QUATER_PI * 3;
            vec2 vertex = p.position; // + r * vec2(cos(angle), sin(angle));
            gl_Position = vec4(vertex, 0, 1);
            EmitVertex();

            Point pLeft = points[leftChild];
            color = cyan;
            angle = QUATER_PI;
            vertex = pLeft.position; // + r * vec2(cos(angle), sin(angle));
            gl_Position = vec4(vertex, 0, 1);
            EmitVertex();
        }

        int rightChild = tree[2 * nodeIndex + 2];
        if(rightChild != -1){
            color = cyan;
            float angle = -QUATER_PI;
            vec2 vertex = p.position; // + r * vec2(cos(angle), sin(angle));
            gl_Position = vec4(vertex, 0, 1);
            EmitVertex();

            Point pRight = points[rightChild];
            color = cyan;
            angle = QUATER_PI * 3;
            vertex = pRight.position; // + r * vec2(cos(angle), sin(angle));
            gl_Position = vec4(vertex, 0, 1);
            EmitVertex();
        }

    }
}