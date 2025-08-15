#version 460 core

#include "raytracing_implicits/implicits.glsl"

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

layout(push_constant) uniform  Constants {
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out Ray ray;
layout(location = 2) out mat4 vModel;

void main(){
    vec2 d = -1 + 2 * uv;
    vec4 direction = (inverse(proj) * vec4(d, 1, 1));
    ray.origin = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    ray.direction = (inverse(view) * vec4(direction.xyz, 0)).xyz;
    vModel = model;
    gl_Position = vec4(pos, 0, 1);
}