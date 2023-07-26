#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) uniform UniformBufferObject{
    mat4 MVP;
    vec3 sun;
    vec3 eyes;
};

layout(location = 0) out vec3 sunDir;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 position_out;
layout(location = 3) out vec3 sunPos;

void main(){
    sunPos = normalize(sun);
    sunDir = sun - eyes;
    vNormal = normal;
    position_out = position.xyz;
    gl_Position = MVP * position;
}