#version 460

const vec3 DEBUG_COLORS[60] = vec3[](
vec3(1.0, 0.0, 0.0), // red
vec3(0.0, 1.0, 0.0), // green
vec3(0.0, 0.0, 1.0), // blue
vec3(1.0, 1.0, 0.0), // yellow
vec3(1.0, 0.0, 1.0), // magenta
vec3(0.0, 1.0, 1.0), // cyan

vec3(1.0, 0.5, 0.0),
vec3(0.5, 1.0, 0.0),
vec3(0.0, 1.0, 0.5),
vec3(0.0, 0.5, 1.0),
vec3(0.5, 0.0, 1.0),
vec3(1.0, 0.0, 0.5),

vec3(0.8, 0.2, 0.2),
vec3(0.2, 0.8, 0.2),
vec3(0.2, 0.2, 0.8),
vec3(0.8, 0.8, 0.2),
vec3(0.8, 0.2, 0.8),
vec3(0.2, 0.8, 0.8),

vec3(1.0, 0.25, 0.25),
vec3(0.25, 1.0, 0.25),
vec3(0.25, 0.25, 1.0),
vec3(1.0, 1.0, 0.25),
vec3(1.0, 0.25, 1.0),
vec3(0.25, 1.0, 1.0),

vec3(0.6, 0.1, 0.1),
vec3(0.1, 0.6, 0.1),
vec3(0.1, 0.1, 0.6),
vec3(0.6, 0.6, 0.1),
vec3(0.6, 0.1, 0.6),
vec3(0.1, 0.6, 0.6),

vec3(1.0, 0.75, 0.25),
vec3(0.75, 1.0, 0.25),
vec3(0.25, 1.0, 0.75),
vec3(0.25, 0.75, 1.0),
vec3(0.75, 0.25, 1.0),
vec3(1.0, 0.25, 0.75),

vec3(0.9, 0.4, 0.1),
vec3(0.4, 0.9, 0.1),
vec3(0.1, 0.9, 0.4),
vec3(0.1, 0.4, 0.9),
vec3(0.4, 0.1, 0.9),
vec3(0.9, 0.1, 0.4),

vec3(0.7, 0.3, 0.3),
vec3(0.3, 0.7, 0.3),
vec3(0.3, 0.3, 0.7),
vec3(0.7, 0.7, 0.3),
vec3(0.7, 0.3, 0.7),
vec3(0.3, 0.7, 0.7),

vec3(1.0, 0.6, 0.6),
vec3(0.6, 1.0, 0.6),
vec3(0.6, 0.6, 1.0),
vec3(1.0, 1.0, 0.6),
vec3(1.0, 0.6, 1.0),
vec3(0.6, 1.0, 1.0),

vec3(0.9, 0.5, 0.3),
vec3(0.5, 0.9, 0.3),
vec3(0.3, 0.9, 0.5),
vec3(0.3, 0.5, 0.9),
vec3(0.5, 0.3, 0.9),
vec3(0.9, 0.3, 0.5)
);

layout(location = 0) in vec3 position;

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUv;

layout(location = 2) flat out int tri;

void main(){
    vColor = vec4(1);
//    vColor.rgb = DEBUG_COLORS[gl_VertexIndex%3];
    tri = gl_VertexIndex%3;
    vColor.rgb = tri == 0 ? vec3(0, 0, 1) : vec3(1);
    gl_Position = proj * view * model * vec4(position, 1);
}