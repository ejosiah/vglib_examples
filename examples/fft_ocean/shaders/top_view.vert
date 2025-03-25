#version 460

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) in vec3 pos;

void main() {
    gl_PointSize = 5.0;
    gl_Position = projection * view * model * vec4(pos, 1);
}