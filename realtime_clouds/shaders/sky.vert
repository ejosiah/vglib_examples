#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 MVP;
    vec3 eyes;
    vec3 sun;
};

layout(location = 0) out vec3 sunDir;
layout(location = 1) out vec3 vNormal;

void main(){
    sunDir = sun - eyes;
    vNormal = normal;

    gl_Position = MVP * position;
}