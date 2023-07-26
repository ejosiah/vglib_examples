#version 460

layout(location = 0) in vec4 position;

layout(push_constant) uniform Constants {
    mat4 MVP;
};

layout(location = 0) out vec3 vUv;

void main(){
    gl_Position = MVP * position;
    vUv = position.xyz + 0.5;
}