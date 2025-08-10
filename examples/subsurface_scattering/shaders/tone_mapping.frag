#version 460

#include "uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 fragColor;

void main(){
    vec4 color = texture(global_textures[uniforms.color_tex_id], uv);

    color.rgb /= color.rgb + 1;
    color.rgb = pow(color.rgb, vec3(0.4545));
    fragColor = color;
}