#version 460 core

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
} fs_in;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 normal;

void main(){
    position = fs_in.position;
    normal = fs_in.normal;
}