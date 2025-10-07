#version 460

#extension GL_EXT_debug_printf : enable

#define RADIANCE_API_ENABLED
#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#define ATMOSPHERE_PARAMS_SET 3
#define ATMOSPHERE_LUT_SET 4
#include "shading.glsl"


layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec2 uv;
} fs_in;

layout(location = 3) noperspective in vec3 distance;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 extras;


vec4 getGradient() {
    return vec4(sampleNormal(fs_in.worldPos.xz), 1);
}


const float far = 25000; // 1km

void main() {
    vec4 grad = getGradient();
    vec3 Nw = vec3(0, 1, 0);
//    vec3 N = sampleNormal(fs_in.worldPos.xz);
    vec3 N = normalize(grad.xyz);
    vec3 L = normalize(u.lightDirection.xyz);
    vec3 V = normalize(fs_in.viewDirection);
    vec3 H = normalize(V + L);

    float depth = linearizeDepth(gl_FragCoord.z, 1, far);
    N = N = mix(N, Nw, 0.8 * min(1.0, sqrt(depth/u.normalFallOff) * 1.1));
    vec3 R = normalize(reflect(-V, N));
    R.y = abs(R.y);

    vec3 turbulence;
    vec3 radiance = shadeBasic(fs_in.worldPos.xyz, N, V, R, H, L);
    vec3 color = mixWireFrame(radiance, distance);

    if(showNormals()) {
        color = N;
    }

    fragColor = vec4(color, 1);
    extras.x = gl_FragCoord.z;
}
