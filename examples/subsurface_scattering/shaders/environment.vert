#version 460

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 texCord;

void main(){
    texCord = position;
    mat4 mView = mat4(mat3(view * model));
    gl_Position = (proj * mView * vec4(position, 1)).xyww;
}