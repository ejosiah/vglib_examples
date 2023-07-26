#version 460

layout(set = 2, binding = 0) uniform SCENE_UBO{
    mat4 inverse_projection;
    mat4 inverse_view;
    vec3 camera;
    vec3 white_point;
    vec3 earth_center;
    vec3 sun_direction;
    vec3 sun_size;
    vec3 kSphereAlbedo;
    vec3 kGroundAlbedo;
    float exposure;
};

layout(location = 0) in vec4 position;

layout(location = 0) out struct {
    vec3 view_ray;
    vec2 uv;
} vs_out;

void main(){
    vec2 uv = .5 * position.xy + .5;
    vec4 target = inverse_projection * position;
    vec3 direction =  (inverse_view * vec4(normalize(target.xyz), 0)).xyz;

    vs_out.uv = uv;
    vs_out.view_ray = direction;
    gl_Position = position;
}