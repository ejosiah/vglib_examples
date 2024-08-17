#version 460

layout(push_constant) uniform Constants {
    mat4 model;
};

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;

void main() {

    gl_Position = model * vec4(position, 0, 1);
}