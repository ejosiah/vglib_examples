#version 460 core
#define PI 3.14159265358979

layout(set = 0, binding = 0) uniform sampler2D temperatureField;

layout(push_constant) uniform Constants{
    float minTemp;
    float maxTemp;
};

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

void main(){
    float temp = min(texture(temperatureField, vUv).x, maxTemp);

    float level = (temp - minTemp)/(maxTemp - minTemp);
    float w = PI * 0.5;
    fragColor.r = sin(w * level);
    fragColor.g = sin(2 * w * level);
    fragColor.b = cos(w * level);
    fragColor.a = 1;
}
