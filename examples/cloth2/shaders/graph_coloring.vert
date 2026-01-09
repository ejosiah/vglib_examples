#version 460

layout(location = 0) flat out int cid;

void main() {
    cid = gl_InstanceIndex;
}