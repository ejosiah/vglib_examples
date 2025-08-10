#version 460

#include "uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main(){
    vec4 diffuse;

    if(uniforms.sss_enabled == 1) {
        diffuse =  texture(global_textures[uniforms.sss_tex_id], uv);
    } else {
        diffuse =  texture(global_textures[uniforms.diffuse_tex_id], uv);
    }

    vec4 specular = texture(global_textures[uniforms.specular_tex_id], uv);

    fragColor = diffuse + specular;
}