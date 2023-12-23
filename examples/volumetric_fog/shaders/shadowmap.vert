#version 460

layout(set = 0, binding = 0) uniform LightSpaceMatrix {
    mat4 lightViewProjection;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

void main(){
    gl_Position = lightViewProjection  * position;
}