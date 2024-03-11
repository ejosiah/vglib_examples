#version 460

layout(set = 0, binding = 1) buffer Points {
    vec2 points[];
};

layout(location = 0) flat in int instanceId;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 generator;

vec3 hash31(float p){
    vec3 p3 = fract(vec3(p) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xxy+p3.yzz)*p3.zyx);
}

void main() {
//    if(instanceId == 2) {
//        fragColor = vec4(1, 0, 0, instanceId);
//    }else{
//        fragColor = vec4(1, 1, 1, instanceId);
//    }
     fragColor = vec4(hash31(instanceId + 1), instanceId);
}