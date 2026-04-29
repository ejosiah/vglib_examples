#version 460

layout(set = 0, binding = 0) buffer OutputImage {
  float image[];    // 28 * 28 pixel image
};

layout(push_constant) uniform Constants {
    vec2 center;
    float radius;
    int isActive;
    int clear;
} brush;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 fragColor;

uint w = 280;

uint pixelIndex() {
    uvec2 pixel = min(uvec2(inUv * vec2(w)), uvec2(w - 1u, w - 1u));
    return pixel.y * w + pixel.x;
}

void main() {
    uint index = pixelIndex();
    float value = image[index];

    if(brush.isActive == 1) {
        float stroke = distance(brush.center, inUv) - brush.radius;
        stroke = 1 - smoothstep(0, 0.05, stroke);

        value += stroke;
    }

    if(brush.clear == 1) {
        value = 0;
    }

    image[index] = value;

    fragColor = vec4(value);
}
