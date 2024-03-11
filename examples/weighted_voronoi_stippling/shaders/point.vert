#version 460

layout(set = 0, binding = 0) buffer Globals {
    mat4 projection;
    int maxPoints;
    int numPoints;
    float convergenceRate;
    int width;
    int height;
};

layout(set = 0, binding = 1) buffer Points {
    vec2 points[];
};

void main() {
    gl_PointSize = 1;
    gl_Position = projection * vec4(points[gl_InstanceIndex], 0, 1);
}