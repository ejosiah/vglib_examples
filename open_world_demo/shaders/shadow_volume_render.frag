#version 460 core

layout(location = 0) out vec4 fracColor;

layout(location = 0) in struct {
    vec3 position;
} fs_in;

void main(){
    fracColor = vec4(1, 0, 0, 0.2);
}