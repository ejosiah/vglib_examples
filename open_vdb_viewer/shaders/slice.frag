#version 460

layout(set = 0, binding = 1) uniform sampler3D volume;


layout(location = 0) in smooth vec3 vUV;
layout(location = 0) out vec4 fragColor;

void main(){
    float value = texture(volume, vUV).r;

    if(value < 0.1) discard;
    fragColor = vec4(value);
}