#version 460

layout(location = 0) in vec2 pos;

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

void main() {
    gl_Position = projection * view * vec4(pos.y, 0, pos.x, 1);
}
