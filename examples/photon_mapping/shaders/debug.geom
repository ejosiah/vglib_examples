#version 460

#define DRAW_MODE_RAY 0
#define DRAW_MODE_CIRCLE 1
#define PI 3.1415

layout(points) in;

layout(line_strip, max_vertices = 128) out;

layout(set = 0, binding = 0) buffer DebugData {
    mat4 transform;

    vec3 position;
    float radius;

    vec3 target;
    int mode;

    vec3 cameraPosition;
    int meshId;

    vec3 pointColor;
    int numNeighbours;

    float pointSize;
    int searchMode;
};

layout(push_constant) uniform Contants {
    mat4 model;
    mat4 view;
    mat4 projection;
};

void main(){
    float delta = 2 * PI/127;

    if(radius > 0 ){
        mat4 MVP = projection * view * transform;
        for (int i = 0; i < 128; i++) {
            float angle = delta * i;
            vec3 vertex = position + radius * vec3(cos(angle), sin(angle), 0);
            gl_Position = MVP * vec4(vertex, 1);
            EmitVertex();
        }
    }
}