#version 460

layout(location = 0) in struct {
    vec2 local_uv;
    vec2 global_uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor = vec4(0, 0, 1, 1);
}