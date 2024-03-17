#version 460

layout(set = 0, binding = 0) buffer Points {
    vec2 generators[];
};

layout(set = 0, binding = 1) buffer Colors {
    vec4 colors[];
};

layout(set = 0, binding = 2) buffer Centers {
    vec2 regions[];
};

layout(set = 0, binding = 3) buffer Density {
    int counts[];
};

layout(push_constant) uniform Constants {
    int renderCentroid;
    float threshold;
    float convergenceRate;
    int screenWidth;
    int screenHeight;
    int numGenerators;
};

layout(location = 0) flat in int instanceId;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = colors[instanceId];
//    fragColor.rgb = gl_FragCoord.zzz;
}