#version 460

struct Region {
    vec2 value;
    int site;
    float weight;
};

layout(set = 0, binding = 2) buffer Sums {
    Region regions[];
};

layout(push_constant) uniform Constants {
    int width;
    int height;
};

layout(location = 0) out vec3 data;

void main() {
    Region region = regions[gl_InstanceIndex];

    vec2 offset = 0.5/vec2(width, height);
    vec2 site = vec2(region.site / width, region.site % width) + offset;
    vec2 position = 2 * site/vec2(width, height) - 1;

    data = vec3(region.value, region.weight);

    gl_PointSize = 1;
    gl_Position =  vec4(position, 0, 1);
}