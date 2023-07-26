#version 450

layout(isolines, equal_spacing, ccw) in;

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
};

layout(location = 0) noperspective out float edge;

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


void main(){
    float u = gl_TessCoord.x;
    vec4 p0 = gl_in[0].gl_Position;
    vec4 p1 = gl_in[1].gl_Position;

    vec4 p = mix(p0, p1, u);

    int id = int(floor(u * gl_TessLevelOuter[1]));

    float lambda = mix(minWaveLength, maxWaveLength, u);
    p.y = getSpd(lambda);
    edge = p.y;

    gl_Position = projection * view * model * p;
}