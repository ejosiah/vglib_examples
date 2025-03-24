#version 460

#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(push_constant) uniform Constants {
    uint texture_id;
    uint use_abs;
};

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 fragColor;

void main() {
    vec4 color = texture(global_textures[texture_id], uv);
    fragColor = use_abs == 1 ? abs(color) : color;
}