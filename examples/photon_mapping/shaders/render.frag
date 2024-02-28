#version 460 core

layout(location = 0) in vec3 iColor;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor.rgb = iColor * 100;
}