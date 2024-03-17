#version 460

layout(points) in;

layout(line_strip, max_vertices = 128) out;

layout(push_constant) uniform Constants {
    vec2 center;
    float radius;
};

layout(location = 0) in flat int instanceId[];

const float PI = 3.1415926;

void main() {

    const int N = 128;
    float delta = (2 * PI)/(N - 1);

    for(int i = 0; i <= N; ++i) {
        float angle = delta * i;
        vec2 vertex = center + radius * vec2(cos(angle), sin(angle));

        gl_Position = vec4(vertex, 0, 1);
        EmitVertex();
    }

}
