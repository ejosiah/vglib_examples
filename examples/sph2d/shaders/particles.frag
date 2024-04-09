#version 460

layout(location = 0) in vec4 iColor;
layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = iColor;
}