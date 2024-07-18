#version 460 core

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out vec2 v_uv;

void main(){
    v_uv = uv;
    gl_Position = vec4(pos, 0, 1);
}