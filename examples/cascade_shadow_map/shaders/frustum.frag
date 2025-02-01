#version 460

layout(location = 0) out vec4 fragColor;

layout(location = 0) in vec3 iColor;

void main() {
    fragColor = vec4(iColor,.4);
}