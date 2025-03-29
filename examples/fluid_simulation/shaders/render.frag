#version 460 core

layout(set = 0, binding = 0) uniform sampler2D image;
layout(set = 1, binding = 0) uniform sampler2D image1;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

float remap(float x, float a, float b, float c, float d) {
    return mix(c, d, (x - a)/(b - a));
}

void main(){
    vec2 uv = vUv;
    vec4 color = vec4(0);

    vec4 a = vec4(0), b = vec4(0);
    a.x = texture(image, uv).x;
    b.x = texture(image1, uv).x;
    if(uv.x < 0.5) {
        uv.x = remap(uv.x, 0, 0.5, 0, 1);
        color = texture(image, uv);
    }else {
        uv.x = remap(uv.x, 0.5, 1, 0, 1);
        color = texture(image1, uv);
    }

    fragColor = color;

}