#version 460 core

layout(binding = 0) uniform sampler3D noise;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

layout(push_constant) uniform Constants {
    vec2 gridSize;
    vec2 uvScale;
    int noiseType;
    int octaves;
    float H;
    int tile;
    int z;
    float time;
};

float remap(float x, float a, float b, float c, float d){
    float t = (x - a)/(b - a);
    return mix(c, d, t);
}

void range(out float min, out float max){
    switch(noiseType){
        case 1:
            min = -1; max = 1;
            break;
        case 0:
        case 2:
            min = 0, max = 1;
        break;
    }
}

void getNoiseRange(out float lower, out float upper){
    float G = exp2(-H);
    float a = 1.0;
    float tMin = 0.0;
    float tMax = 0.0;
    float min, max;
    range(min, max);
    for(int i = 0; i < octaves; i++){
        tMin += a * min;
        tMax += a * max;
        a *= G;
    }
    lower = tMin;
    upper = tMax;
}

void main(){
    vec3 u;
    u.xy = vUv * uvScale;
    u.z = (0.5 + float(z))/textureSize(noise, 0).z;

    vec4 noiseComp = texture(noise, u);
    float perlinWorly = noiseComp.x;
    float wfbm = dot(vec3(.625, .125, .25), noiseComp.yzw);
    float cloud = remap(perlinWorly, wfbm - 1, 1, 0, 1);
    cloud = remap(cloud, .5, 1, 0, 1);

    fragColor = vec4(cloud);
}