#version 460

struct Light{
    vec3 direction;
    float range;

    vec3 color;
    float intensity;

    vec3 position;
    float innerConeCos;

    float outerConeCos;
    int type;
};


layout(set = 0, binding = 0) buffer PunctualLights {
    Light lights[];
};


void main() {

}