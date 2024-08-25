#version 460

#extension GL_EXT_nonuniform_qualifier : require

#include "scene.glsl"

#define ONE_OVER_4PI	0.0795774715459476

// NOTE: also defined in vertex shader
#define BLEND_START		8		// m
#define BLEND_END		200		// m

layout(set = 2, binding = 10) uniform sampler2D global_textures[];
layout(set = 2, binding = 10) uniform sampler3D global_textures3d[];

#define GRADIENT_MAP (global_textures[scene.gradient_texture_id])

#define RADIANCE_API_ENABLED
#define BIND_LESS_ENABLED
#define ATMOSPHERE_PARAMS_SET 1
#include "atmosphere_api.glsl"

layout(location = 0) in vec3 worldPos;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) flat in int patchId;

layout(location = 0) out vec4 fragColor;

const vec3 deepBlue = vec3(0.0056f, 0.0194f, 0.0331f);
const vec3 carribbean = vec3(0.1812f, 0.4678f, 0.5520f);
const vec3 lightBlue = vec3(0.0000f, 0.2307f, 0.3613f);

vec3 oceanColor = carribbean;

vec3 hash31(float p){
    vec3 p3 = fract(vec3(p) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx+33.33);
    return fract((p3.xxy+p3.yzz)*p3.zyx);
}

vec3 upwelling = vec3(0, 0.2, 0.3);
vec3 sky = vec3(0.69,0.84,1);
vec3 air = vec3(0.1,0.1,0.1);
float nSnell = 1.34;
float Kdiffuse = 0.91;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

const vec3 sunColor	= vec3(1.0, 1.0, 0.47);

void main() {
    fragColor = vec4(1);

    vec3 F0 = vec3(0.02);

    vec4 grad = texture(GRADIENT_MAP, uv);

    vec3 viewDir = scene.camera - worldPos;

    vec3 N = normalize(grad.xzy);
    //    vec3 N = normalize(normal);
    vec3 L = normalize(scene.sunDirection);
    vec3 E = normalize(viewDir);
    vec3 H = normalize(E + L);
    vec3 R = reflect(-E, N);

    vec3 F = fresnelSchlick(max(dot(N,R), 0), F0);

    float dist = length(viewDir) * Kdiffuse;
    dist = exp(dist);

    vec3 color = dist * (F * sky + (1 - F) * upwelling) + (1 -dist) * air;

    fragColor.rgb = color;
}