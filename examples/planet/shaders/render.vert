#version 460

layout(location = 0) in vec3 position;


layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;


void main(){
    vColor = vec4(1);
    vUv = position.xz;
    gl_PointSize = 2.0;
    gl_Position = proj * view * model * vec4(position, 1);
}