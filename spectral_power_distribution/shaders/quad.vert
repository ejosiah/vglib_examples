#version 460

layout(location = 0) in vec2 position;

layout(set = 0, binding = 2) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

void main(){
    vec2 p = position;
    gl_Position = vec4(p, 0.9, 1);
}