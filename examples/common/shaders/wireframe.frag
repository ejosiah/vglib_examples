#version 460 core

layout(push_constant) uniform Color {
    layout(offset=192)
    vec3 color;
};

layout(location = 0) out vec4 fracColor;

void main(){
    fracColor = vec4(color, 1);
}