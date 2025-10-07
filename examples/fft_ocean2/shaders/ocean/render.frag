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

vec3 colors[3] = vec3[3](
    vec3(0.0056f, 0.0194f, 0.0331f),	// deep blue
    vec3(0.1812f, 0.4678f, 0.5520f),	// carribbean
    vec3(0.0000f, 0.2307f, 0.3613f)	// light blue
);

vec3 getNormal() {
    vec3 N = sampleNormal(fs_in.worldPos.xz);

//    N = sampleJacobianNormal(fs_in.worldPos.xz);
    return normalize(N);
}


vec3 oceanColor = colors[0];

const float far = 25000; // 1km

void main() {
    const float PI = 3.1415926535;
    vec3 Nw = vec3(0, 1, 0);
    vec3 N = sampleNormal(fs_in.worldPos.xz);
    vec3 L = normalize(u.lightDirection.xyz);
    vec3 V = normalize(fs_in.viewDirection);

    float depth = linearizeDepth(gl_FragCoord.z, 1, far);
    N = N = mix(N, Nw, 0.8 * min(1.0, sqrt(depth * 0.01) * 1.1));
    vec3 R = normalize(reflect(-V, N));
    R.y = abs(R.y);

    vec3 turbulence;
    vec3 radiance = shadeBasic(fs_in.worldPos.xyz, N, V, R, L, oceanColor);
    vec3 color = mixWireFrame(radiance, distance);

    if(showNormals()) {
        color = N;
    }

    fragColor = vec4(color, 1);
    extras.x = gl_FragCoord.z;
}
