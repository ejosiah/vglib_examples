#version 460

layout(location = 0) in vec4 position;

layout(set = 1, binding = 0) uniform UBO{
    mat4 model_from_view;
    mat4 view_from_clip;
    vec3 camera;
    vec3 white_point;
    vec3 earth_center;
    vec3 sun_direction;
    vec3 sun_size;
    vec3 kSphereAlbedo;
    vec3 kGroundAlbedo;
    float exposure;
};

layout(location = 0) out struct {
    vec3 view_ray;
    vec2 uv;
} vs_out;

void main(){
    vs_out.uv = .5 * position.xy + .5;
    vec4 vertex = position;
    vertex.y *= -1;
    vs_out.view_ray = (model_from_view * vec4((view_from_clip * vertex).xyz, 0.0)).xyz;
    gl_Position = position;
}