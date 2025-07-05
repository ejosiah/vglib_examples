#version 460 core

layout(set = 0, binding = 0) uniform sampler2D position;
layout(set = 0, binding = 1) uniform sampler2D normal;
layout(set = 0, binding = 2) uniform sampler2D ambient_occlusion;

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main(){
    fragColor = texture(ambient_occlusion, vUv).rrrr;
}