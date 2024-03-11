#version 460

layout(points) in;

layout(triangle_strip, max_vertices = 128) out;


const float PI = 3.1415926;

void main() {
    vec2 center = gl_in[0].gl_Position.xy;

    int numSectors = 1;
    float angleDelta = (2 * PI)/numSectors;
    float radius = 0.0050;

    const int N = 12;
    float delta = (2 * PI)/(N - 1);

    for(int i = 0; i <= N; ++i) {
        if(i % 3 == 0) {
            gl_Position = vec4(center, 0, 1);
            EmitVertex();
        }

        float angle = delta * i;
        vec2 vertex = center + radius * vec2(cos(angle), sin(angle));

        gl_Position = vec4(vertex, 0, 1);
        EmitVertex();
    }

}
