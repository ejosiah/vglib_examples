#version 460

layout(push_constant) uniform SHADOW_CONSTANTS{
    mat4 lightSpaceMatrix;
};

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tangent;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

void main(){
    gl_Position = lightSpaceMatrix * position;
}