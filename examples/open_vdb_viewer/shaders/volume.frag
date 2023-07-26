#version 460

layout(set = 1, binding = 0) uniform UBO {
    vec3 boxMin;
    vec3 boxMax;
    float time;
} ubo;

layout(set = 1, binding = 1) uniform sampler3D volume;

layout(location = 0) in struct {
    vec2 uv;
    vec3 ray_direction;
} fs_in;

layout(location = 0) out vec4 fragColor;

void main(){
//    float z = fract(ubo.time * 0.1);
//    fragColor = texture(volume, vec3(fs_in.uv, z)).rrrr;
//    if(fragColor.r < 0.001) discard;

    fragColor.rgb = normalize(fs_in.ray_direction);
}