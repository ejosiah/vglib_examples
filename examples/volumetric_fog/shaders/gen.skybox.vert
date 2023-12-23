#version 460

layout(push_constant) uniform Camera {
    mat4 transform;
} cam;

layout(location = 0) in vec3 position;

layout(location = 0) out struct {
    vec3 direction;
} vs_out;

void main() {
    vs_out.direction = position;
    gl_Position = cam.transform * vec4(position, 1);
}