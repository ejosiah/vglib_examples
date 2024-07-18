#version 460
#extension GL_EXT_nonuniform_qualifier : enable

#define RENDER_BUFFER global_textures[g_buffer_texture_id]

#include "uniforms.glsl"

layout(set = 1, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in vec2 f_uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 color = texture(RENDER_BUFFER, f_uv).rgb;

    if(tone_map == 1 ){
        color.rgb /= 1 + color.rgb;
        color.rgb = pow(color.rgb, vec3(0.4545));
    }
    fragColor = vec4(color, 1);
}