#version 460 core
#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec4 color;
layout(location = 5) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

struct Light{
    vec3 position;
    vec3 direction;
    vec3 color;
    float range;
    float intensity;
    float innerConeCos;
    float outerConeCos;
    int type;
    int shadowMapIndex;
};

layout(set = 0, binding = 0, scalar) buffer PunctualLights {
    Light lights[];
};


void main(){
    vColor.rgb = lights[0].color;
    vUv = uv;
    gl_PointSize = 2.0;
    vec4 lightPosition = vec4(lights[0].position + position.xyz, 1);
    gl_Position = proj * view * model * lightPosition;
}