#version 460

#define HF_TEXTURE_ID 7

layout(set = 0, binding = 10) uniform sampler2D global_textures[];

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 fragColor;

void main(){
    vec3 offset = texture(global_textures[HF_TEXTURE_ID], uv).rgb;
    vec3 color = vec3(offset.y);

    color *= 10;
    color /= color + 1;

    fragColor = vec4(1);
}