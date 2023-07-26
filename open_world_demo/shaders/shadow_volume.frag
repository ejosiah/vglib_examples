#version 460

layout(input_attachment_index=0, set=0, binding=1) uniform subpassInput shaodwIn;

layout(location = 0) in struct {
    vec3 position;
} fs_in;

layout(location = 0) out vec4 shadowInOut;

void main(){
    shadowInOut.x = subpassLoad(shaodwIn).x;
    shadowInOut.y = fs_in.position.z;
}