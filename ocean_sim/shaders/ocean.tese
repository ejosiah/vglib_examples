#version 460

layout(quads, fractional_even_spacing, ccw) in;

layout(set = 0, binding = 0) uniform sampler2D heightMap;

layout(push_constant) uniform CAMERA {
    mat4 model;
    mat4 view;
    mat4 projection;
} camera;

layout(location = 0) out struct {
    vec2 local_uv;
    vec2 global_uv;
} tes_out;

void main(){
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;
    vec4 p2 = gl_in[2].gl_Position;
    vec4 p3 = gl_in[3].gl_Position;

    vec4 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);

    tes_out.local_uv = gl_TessCoord.xy;
    tes_out.global_uv = p.xz + .5;

    gl_Position = camera.projection * camera.view * camera.model * p;
}