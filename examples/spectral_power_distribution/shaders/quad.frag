#version 460

#define H_PI 1.5707963267948966192313216916398

layout(set = 0, binding = 0) buffer SPD_WAVE_LENGTHS {
    float waveLengths[];
};

layout(set = 0, binding = 1) buffer SPD_VALUES {
    float values[];
};

layout(set = 0, binding = 2) uniform MVP {
    mat4 model;
    mat4 view;
    mat4 projection;
};

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


float getSpd(float lambda){
    int first = 0;
    int len = numBins;

    // binary search wavelengths range for lambda
    while(len > 0){
        int _half = len >> 1;
        int middle = first + _half;
        if(waveLengths[middle] <= lambda){
            first = middle + 1;
            len -= _half + 1;
        }else {
            len = _half;
        }
    }

    int index = clamp(first - 1, 0, numBins - 2);
    float lambdaStart = waveLengths[index];
    float lambdaEnd = waveLengths[index+1];
    float t = (lambda - lambdaStart)/(lambdaEnd - lambdaStart);

    t = smoothstep(0, 1, t);

    float lower = values[index];
    float upper = values[index+1];

    return mix(lower, upper, t);
}

float remap(float x, float a, float b, float c, float d){
    float t = (x - a)/(b - a);
    return mix(c, d, t);
}

layout(location = 0) out vec4 fragColor;

void main(){
    float u = gl_FragCoord.x/resolution.x;
    float v = (resolution.y - gl_FragCoord.y)/resolution.y;

    vec3 col;
    col.r = sin(u * H_PI);
    col.g = sin(2. * u * H_PI);
    col.b = cos(u * H_PI);

    float lambda = mix(minWaveLength, maxWaveLength, u);
    float edge = getSpd(lambda);
    edge = remap(edge, minValue, maxValue, 0, 1);

    float w = fwidth(v);
    float t = smoothstep(edge - 0.01, edge, v);

    fragColor.rgb = mix(col, vec3(0), t);
}