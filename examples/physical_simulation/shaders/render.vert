#version 460

layout(set = 0, binding = 0) buffer SIM_POSITIONS {
    vec4 positions[];
};

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 uv;

void main() {
    vec4 p = positions[gl_InstanceIndex];

    color.rgb = mix(vec3(0, 0.2, 1), vec3(0.2, 0.05, 0), p.w);
    color.a = 1;
    gl_PointSize = 1;
    gl_Position = projection * view * model * vec4(p.xyz, 1);
}