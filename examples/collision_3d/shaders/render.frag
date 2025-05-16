#version 460 core

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
    vec3 color;
} fs_in;

layout(location = 0) out vec4 fracColor;

void main(){

    fracColor.rgb = fs_in.color;
}