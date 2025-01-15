#version 450
#extension GL_GOOGLE_include_directive : enable
#define PI 3.14159265358979


layout(quads, equal_spacing, ccw) in;

layout(push_constant) uniform Constants {
    mat4 model;
    mat4 view;
    mat4 projection;
} cam;

layout(location = 0) out struct {
    vec3 normal;
    vec2 uv;
} v_out;

void main(){
    float u = gl_TessCoord.x;
    float v = gl_TessCoord.y;

    vec3 p0 = gl_in[0].gl_Position.xyz;
    vec3 p1 = gl_in[1].gl_Position.xyz;
    vec3 p2 = gl_in[2].gl_Position.xyz;
    vec3 p3 = gl_in[3].gl_Position.xyz;

    vec3 p = mix(mix(p0, p1, u), mix(p3, p2, u), v);
    vec3 n;

    n = cross(p1.xyz - p0.xyz, p2.xyz - p0.xyz);
    v_out.uv = vec2(u, v);

    v_out.normal = n;

    gl_Position = cam.projection * cam.view * cam.model * vec4(p, 1);

}