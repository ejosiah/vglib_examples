#version 460

layout(location = 0) in vec2 clipPos;
layout(location = 1) in vec2 uv;

layout(set = 0, binding = 0) uniform Info {
    mat4 inverse_model;
    mat4 inverse_view;
    mat4 inverse_projection;

    vec4 camera;
    vec4 earthCenter;
    vec4 sunDirection;
    vec4 whitePoint;
    vec2 sunSize;
    float exposure;
};

layout(location = 0) out struct {
    vec3 position;
    vec3 direction;
} vs_out;

void main() {
    vec4 viewPos = inverse_projection * vec4(clipPos, 1, 1);
    vec4 viewDirection =  inverse_view * vec4(viewPos.xyz, 0);

    vs_out.position = viewPos.xyz;
    vs_out.direction = viewDirection.xyz;

    gl_Position = vec4(clipPos, 0, 1);
}