#version 460

layout(set = 0, binding = 0) uniform sampler3D volume;

layout(push_constant) uniform Constants {
    layout(offset = 64)
    vec4 camPos;
    vec4 stepSize;
};


layout(location = 0) in  vec3 vUV;
layout(location = 0) out vec4 fragColor;

const vec3 texMin = vec3(0);
const vec3 texMax = vec3(1);


void main(){
    vec3 dataPos = vUV;
    vec3 geomDir = normalize((dataPos - 0.5) - camPos.xyz);

    vec3 size = textureSize(volume, 0);
    vec3 dirStep = geomDir * (1/size);
    fragColor = vec4(0);
    int maxSamples = int(max(size.x, max(size.y, size.z)));
    for(int i = 0; i < maxSamples; i++){
        dataPos += dirStep;

        bool stop = dot(sign(dataPos - texMin), sign(texMax - dataPos)) < 3;

        if(stop) break;

        float voxel = texture(volume, dataPos).r;

        float prev_alpha = voxel - (voxel * fragColor.a);
        fragColor.rgb = prev_alpha * vec3(voxel) + fragColor.rgb;
        fragColor.a += prev_alpha;

        if(fragColor.a > 0.99) break;
    }
}