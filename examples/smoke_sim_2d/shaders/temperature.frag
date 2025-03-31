#version 460 core
#define PI 3.14159265358979

layout(set = 0, binding = 0) uniform sampler2D temperatureField;
layout(set = 1, binding = 0) uniform sampler2D temperatureField1;

layout(push_constant) uniform Constants{
    float minTemp;
    float maxTemp;
};

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

float remap(float x, float a, float b, float c, float d) {
    return mix(c, d, (x - a)/(b - a));
}


void main(){
    float temp = 0;

    vec2 uv = vUv;
    if(uv.x < 0.5) {
        uv.x = remap(uv.x, 0, 0.5, 0, 1);
        temp = min(texture(temperatureField, uv).x, maxTemp);
    }else {
        uv.x = remap(uv.x, 0.5, 1, 0, 1);
        temp = min(texture(temperatureField1, uv).x, maxTemp);
    }

    float level = (temp - minTemp)/(maxTemp - minTemp);
    float w = PI * 0.5;
    fragColor.r = sin(w * level);
    fragColor.g = sin(2 * w * level);
    fragColor.b = cos(w * level);
    fragColor.a = 1;
}