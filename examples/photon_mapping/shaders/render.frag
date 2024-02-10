#version 460 core
#extension GL_EXT_scalar_block_layout : enable

#include "ltc.glsl"
#include "sampling.glsl"

const vec3 globalAmbient = vec3(0);
const vec3 Light = vec3(9.9308214188, 3.7897210121, 0.6708359718);

layout(constant_id = 0) const uint materialOffset = 192;

layout(set = 0, binding = 0) uniform MATERIAL {
    vec3 diffuse;
    vec3 ambient;
    vec3 specular;
    vec3 emission;
    vec3 transmittance;
    float shininess;
    float ior;
    float opacity;
    float illum;
};

layout(set = 1, binding = 0) buffer LIGHT {
    vec3 position;
    vec3 normal;
    vec3 intensity;
    vec3 lowerCorner;
    vec3 upperCorner;
} light;

layout(set = 1, binding = 1) uniform sampler2D ltc_mat;
layout(set = 1, binding = 2) uniform sampler2D ltc_amp;

layout(location = 0) in vec3 vPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 eyes;

layout(location = 0) out vec4 fragColor;

float saturate(float x){
    return max(0, x);
}

void initPoints(out vec3 points[4]) {
    vec3 lc = light.lowerCorner;
    vec3 uc = light.upperCorner;
    points[0] = lc;
    points[1] = lc + vec3(uc.x, 0, 0);
    points[2] = lc + vec3(0, 0, uc.z);
    points[3] =  uc;

}

uint numSamples = 100;

vec3 computeRadiance(vec3 lightPos) {
    vec3 lightDir = lightPos - vPos;
    vec3 L = normalize(lightDir);

    vec3 N = normalize(vNormal);
    vec3 E = normalize(eyes - vPos);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-L, N);

    float dist = length(lightDir);
    vec3 radiance = light.intensity;
    radiance;

    return emission + radiance * saturate(dot(L, N)) * diffuse;
}

void main(){
    vec3 color = vec3(0);
    vec3 dim = light.upperCorner - light.lowerCorner;
    for(uint i = 0; i < numSamples; i++){
        vec2 point = hammersley(i, numSamples);
        vec3 lightPos = light.lowerCorner + dim * vec3(point.x, 0, point.y);
        color += computeRadiance(lightPos);
    }
    color /= float(numSamples);
    color /= (color + 1);
    fragColor = vec4(color, 1);
}