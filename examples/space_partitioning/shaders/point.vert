#version 460

#include "domain.glsl"

layout(set = 0, binding = 0) buffer Points {
    Point points[];
};

layout(location = 0) out flat int instanceId;
layout(location = 1) out vec3 color;

void main() {
    Point p = points[gl_InstanceIndex];

    instanceId = gl_InstanceIndex;
    color = p.color.rgb;

    gl_PointSize = 2;
    gl_Position = vec4(p.position, 0, 1);
}