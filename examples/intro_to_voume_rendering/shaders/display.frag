#version 460 core

layout(set = 0, binding = 10) uniform sampler2D image;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main(){
    vec3 color = texture(image, vUv).rgb;
    color /= color + 1;
    color = pow(color, vec3(0.454));
    fragColor = vec4(color, 1);
}