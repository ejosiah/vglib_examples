#version 460

layout(location = 0) in vec3 position;

layout(location = 0) out vec3 texCord;

layout(set = 1, binding = 0) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
    int brdf_lut_texture_id;
    int irradiance_texture_id;
    int specular_texture_id;
    int framebuffer_texture_id;
    int discard_transmissive;
    int environment;
    int tone_map;
};

void main(){
    texCord = position;
    mat4 mView = mat4(mat3(view * model));
    gl_Position = (projection * mView * vec4(position, 1)).xyww;
}