#version 460
#extension GL_EXT_multiview : require

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(set = 3, binding = 0) uniform Camera {
    mat4 transform[6];
} cam;


layout(location = 0) out struct {
    vec3 direction;
} gs_out;

void main() {
    gl_Layer = gl_ViewIndex;

    for(int i = 0; i < gl_in.length(); ++i) {
        gs_out.direction = gl_in[i].gl_Position.xyz;
        gl_Position = cam.transform[gl_ViewIndex] * gl_in[i].gl_Position;
        EmitVertex();
    }

}