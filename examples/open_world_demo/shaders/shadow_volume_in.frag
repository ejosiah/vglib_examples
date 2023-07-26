#version 460

layout(location = 0) in struct {
    vec3 position;
} fs_in;


layout(location = 0) out vec4 shadowIn;

void main(){
    shadowIn.x = fs_in.position.z;
}