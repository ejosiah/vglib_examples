#version 460

layout(location = 0) in struct {
    vec3 color;
    vec2 local_uv;
    vec2 global_uv;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor = vec4(fs_in.color, 1);
}