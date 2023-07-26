#version 460

layout(location = 0) in vec4 position;

layout(location = 0) out int layer;

void main(){
    layer = gl_InstanceIndex;
    gl_Position = position;
}