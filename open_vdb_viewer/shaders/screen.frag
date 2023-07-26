#version 460

layout(set = 0, binding = 0) uniform sampler2D image;

layout(location = 0) in struct {
    vec2 uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor.rgb = texture(image, fs_in.uv).rgb;
}