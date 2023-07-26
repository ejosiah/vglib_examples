#version 460 core

layout(push_constant) uniform Constants {
    float exposure;
};

layout(binding = 0) uniform sampler2D image;
layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 fragColor;

float linearToSrgb(float linearColor)
{
    if (linearColor < 0.0031308f) return linearColor * 12.92f;
    else return 1.055f * float(pow(linearColor, 1.0f / 2.4f)) - 0.055f;
}
vec3 linearToSrgb(vec3 c){
    return vec3(linearToSrgb(c.r),linearToSrgb(c.g), linearToSrgb(c.b));
}


void main(){
    vec3 color = texture(image, vUv).rgb;
    color = 1 - exp(-color * exposure);
    color = linearToSrgb(color);
    fragColor.rgb = color;
}