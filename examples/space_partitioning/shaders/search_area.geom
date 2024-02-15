#version 460

#define PI 3.1415

layout(points) in;

layout(line_strip, max_vertices = 128) out;

layout(location = 0) flat in float vRadius[];

layout(location = 1) out vec3 color;

void main(){

    vec2 center = gl_in[0].gl_Position.xy;
    float radius = vRadius[0];
    float delta = 2 * PI/127;

    for(int i = 0; i < 128; i++) {
        float angle = delta * i;
        vec2 vertex = center + radius * vec2(cos(angle), sin(angle));
        color = vec3(1, 1, 0);
        gl_Position = vec4(vertex, 0, 1);
        EmitVertex();
    }
    EndPrimitive();
}