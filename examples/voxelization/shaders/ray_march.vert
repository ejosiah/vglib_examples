#version 460

layout(location = 0) in vec2 pos;
layout(location = 1) in vec2 uv;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 min;
    vec4 max;
};

layout(location = 0) out struct {
    vec3 direction;
} vs_out;

void main() {
    vec4 rd = inverse(proj) * vec4(pos.xy, 1, 1);
    rd /= rd.w;
    rd = inverse(view) * rd;
    vs_out.direction = normalize(rd.xyz);
    gl_Position = vec4(pos, 0, 1);
}