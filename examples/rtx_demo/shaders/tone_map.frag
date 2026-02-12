#version 460

#extension GL_EXT_nonuniform_qualifier : enable

#include "tone_mapping.glsl"

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout (location = 0) in vec2 uv;
layout (location = 1) flat in uint texture_id;

layout(location = 0) out vec4 fragColor;

void main() {
    vec3 color = texture(global_textures[nonuniformEXT(texture_id)], uv).rgb;

    color = tone_map(color, Uncharted2);
    color = pow(color, vec3(0.454));

    fragColor = vec4(color, 1);
}