#version 460

layout(set = 0, binding = 0) uniform sampler2D sourceImage;
layout(set = 1, binding = 6) uniform sampler2D voronoiImage;

layout(set = 1, binding = 0) buffer Globals {
    mat4 projection;
    int maxPoints;
    int numPoints;
    float convergenceRate;
    int width;
    int height;
};

layout(set = 1, binding = 1) buffer Points {
    vec2 generators[];
};

layout(set = 1, binding = 2) buffer Sums {
    vec2 regions[];
};

layout(set = 1, binding = 3) buffer Density {
    int counts[];
};

layout(set = 1, binding = 4) buffer Centers {
    vec2 centroids[];
};

layout(location = 0) out vec4 fragColor;

void main(){
    fragColor = vec4(0);
}