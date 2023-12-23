#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 tanget;
layout(location = 3) in vec3 bitangent;
layout(location = 4) in vec3 color;
layout(location = 5) in vec2 uv;

layout(set = 0, binding = 0) buffer XFORM {
    mat4 models[];
};

layout(push_constant) uniform Camera {
  mat4 model;
  mat4 view;
  mat4 projection;
};

layout(location = 0) out struct {
    vec3 normal;
    vec3 fragPos;
    vec3 camPos;
} vs_out;

void main() {
    mat4 model = models[gl_InstanceIndex];
    vs_out.normal =  normal;
    vs_out.camPos = (inverse(view) * vec4(0, 0, 0, 1)).xyz;
    vs_out.fragPos = (model * position).xyz;
    gl_Position = projection * view * model * position;
}