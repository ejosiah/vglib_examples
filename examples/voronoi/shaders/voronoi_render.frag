#version 460

layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform Constants {
    int renderCentroid;
    float threshold;
    float convergenceRate;
    int screenWidth;
    int screenHeight;
    int numGenerators;
};

layout(location = 0) out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy/vec2(screenWidth, screenHeight);
    fragColor.rgb = texture(colorTex, uv).rgb;
}