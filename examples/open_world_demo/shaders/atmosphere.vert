#version 460

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;

layout(set = 1, binding = 0) uniform UBO{
    mat4 viewToWorldSpaceMatrix;
    mat4 clipToViewSpaceMatrix;
    vec3 camera;
    vec3 white_point;
    vec3 earth_center;
    vec3 sun_direction;
    vec3 sun_size;
    float exposure;
};

layout(location = 0) out struct {
    vec3 view_ray;
    vec2 uv;
} vs_out;

void main(){
    vs_out.uv = uv;

    vec4 direction = vec4(position.xy, 1, 1);
    direction = clipToViewSpaceMatrix * direction;
    vs_out.view_ray = (viewToWorldSpaceMatrix * direction).xyz;

    gl_Position = position;
}