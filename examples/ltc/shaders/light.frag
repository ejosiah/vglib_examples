#version 460 core

#extension GL_EXT_scalar_block_layout : enable

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUv;

layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0, scalar) uniform Options {
    mat4  transform;
    vec3 eyes;
    vec2 resolution;
    int   sampleCount;
    float roughness;
    vec3  dcolor;
    vec3  scolor;

    float intensity;
    float width;
    float height;
    float roty;
    float rotz;

    int twoSided;
};

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

void main(){
    vec3 col = texture(global_textures[2], vUv).rgb * intensity;
    col /= col + 1;
    fragColor = vec4(col, 1);
}