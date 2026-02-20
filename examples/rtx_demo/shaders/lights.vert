#version 460 core

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

#define LIGHT_SET 0
#define LIGHT_BINDING_POINT 0
#define LIGHT_INSTANCE_BINDING_POINT 1
#include "gltf/lights_descriptor.glsl"

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;


void main(){
    Light light = lightAt(gl_InstanceIndex);

    vColor = vec4(light.color.rgb, 1);
    vUv = uv;
    gl_Position = proj * view * model * vec4(light.position + position.xyz * 0.1, 1);
}