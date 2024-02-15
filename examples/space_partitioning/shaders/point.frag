#version 460

layout(location = 0) out vec4 fragColor;

layout(location = 1) in vec3 color;

void main(){
    fragColor = vec4(color, 1);
}