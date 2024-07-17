#version 460

#define UNIFORMS_SET 1
#define UNIFORMS_BINDING_POINT 0
#include "uniforms.glsl"

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 texCord;

void main(){
    texCord = position;
    mat4 mView = mat4(mat3(view * model));
    gl_Position = (projection * mView * vec4(position, 1)).xyww;
}