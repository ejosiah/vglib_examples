#version 460


layout(set = 0, binding = 0) uniform UBO {
    mat4 projection;
    mat4 view;
    mat4 inv_projection;
    mat4 inv_view;
} ubo;

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

layout(location = 0) out struct {
    vec2 uv;
    vec3 ray_direction;
} vs_out;

void main(){
    vs_out.uv = uv;
    vec4 rd = vec4(pos, 1, 1);
    rd = ubo.inv_projection * rd;
    rd /= rd.w;
    rd = ubo.inv_view * normalize(rd);
    vs_out.ray_direction = rd.xyz;
    gl_Position = vec4(pos, 0, 1);
}