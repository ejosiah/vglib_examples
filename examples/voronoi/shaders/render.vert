#version 460

layout(location = 0) flat out int instanceId;

void main() {
    gl_PointSize = 1;
    instanceId = gl_InstanceIndex;
    gl_Position = vec4(0, 0, 0, 1);
}