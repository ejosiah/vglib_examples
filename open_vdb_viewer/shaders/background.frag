#version 460

layout(location = 0) in struct {
    vec2 uv;
    vec3 ray_direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor.rgb = mix(vec3(1), vec3(0, 0.3, 0.8), 1 - fs_in.uv.y);
}