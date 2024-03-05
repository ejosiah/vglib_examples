#version 460

layout(points) in;

layout(triangle_strip, max_vertices = 128) out;

layout(set = 0, binding = 0) buffer Points {
    vec2 points[];
};

layout(set = 0, binding = 4) buffer Centers {
    vec2 centroids[];
};

layout(push_constant) uniform Constants {
    int blockSize;
    int renderCentroid;
    float threshold;
    float convergenceRate;
    int screenWidth;
    int screenHeight;
};

layout(location = 0) in flat int instanceId[];

const float PI = 3.1415926;

void main() {
    int idx = instanceId[0];
    vec2 center =  renderCentroid == 1 ? centroids[idx] : points[idx];
    int numSectors = 1;
    float angleDelta = (2 * PI)/numSectors;
    float radius = renderCentroid == 1 ? 0.0025 : 0.0050;

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
