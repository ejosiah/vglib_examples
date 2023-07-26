#version 460

layout(location = 0) in vec3 pos;

layout(push_constant) uniform CAMERA {
    mat4 model;
    mat4 view;
    mat4 projection;
} camera;

layout(location = 0) out struct {
    vec3 pos;
} vs_out;

void main(){
    vs_out.pos = pos;
    mat4 rotView = mat4(mat3(camera.view));
    gl_Position = (camera.projection * rotView * vec4(pos, 1)).xyww;
}