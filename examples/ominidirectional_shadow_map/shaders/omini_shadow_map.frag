#version 460

layout(location = 0) in struct {
    vec3 viewSpacePos;
} fs_in;

layout(location = 2) in flat float farPlane;

void main() {
    gl_FragDepth = length(fs_in.viewSpacePos)/farPlane;
}