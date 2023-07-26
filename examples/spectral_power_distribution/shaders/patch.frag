#version 450

#define H_PI 1.5707963267948966192313216916398

layout(push_constant) uniform Constants{
layout(offset=16)
    vec4 color;
    vec2 resolution;
    float minValue;
    float maxValue;
    float minWaveLength;
    float maxWaveLength;
    int numBins;
    int lineResolution;
};

layout(location = 0) noperspective in float edge;
layout(location = 0) out vec4 fragColor;


float remap(float x, float a, float b, float c, float d){
    float t = (x - a)/(b - a);
    return mix(c, d, t);
}

void main(){
    float level = gl_FragCoord.x/resolution.x;
//    fragColor.r = sin(level * H_PI);
//    fragColor.g = sin(2. * level * H_PI);
//    fragColor.b = cos(level * H_PI);
//    fragColor.a = 1;
    fragColor = color;
}