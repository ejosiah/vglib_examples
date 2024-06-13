#version 460

#extension GL_ARB_shader_viewport_layer_array : enable
#extension GL_EXT_multiview : enable

layout(location = 0) in vec3 position;

layout(set = 3, binding = 0) uniform Camera {
    mat4 transform[6];
} cam;

layout(location = 0) out struct {
    vec3 direction;
} vs_out;

void main() {
    vs_out.direction = position;
    gl_Layer = gl_ViewIndex;
    gl_Position = cam.transform[gl_ViewIndex] * vec4(position, 1);
}