#version 460

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 10)  uniform sampler2DArray global_textures[];

layout(push_constant) uniform Constants {
    uint index;
};

layout(location = 0) in vec2 i_uv;
layout(location = 0) out vec4 fragColor;

void main() {
    vec2 gid = floor(i_uv * 2);
    vec2 uv = fract(i_uv * 2);
    uv.y = 1 - uv.y;

    float layer = gid.y * 2 + gid.x;
    vec3 loc = vec3(uv, layer);
    vec4 color = texture(global_textures[nonuniformEXT(index)], loc);
    fragColor = color;
}