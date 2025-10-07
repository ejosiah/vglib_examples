#version 460

#extension GL_EXT_debug_printf : enable

#define BINDLESS_DESCRIPTOR_SET 1
#define OCEAN_UNIFORM_SET 2
#include "shading.glsl"

layout(location = 0) in struct {
    vec3 worldPos;
    vec3 viewDirection;
    vec2 uv;
} fs_in;

layout(location = 3) noperspective in vec3 distance;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 extras;

vec3 oceanColor[3] = vec3[3](
    vec3(0.0056f, 0.0194f, 0.0331f),	// deep blue
    vec3(0.1812f, 0.4678f, 0.5520f),	// carribbean
    vec3(0.0000f, 0.2307f, 0.3613f)	// light blue
);

vec3 getNormal() {
    vec3 N = sampleNormal(fs_in.worldPos.xz);

//    N = sampleJacobianNormal(fs_in.worldPos.xz);
    return normalize(N);
}

const vec3 sunColor	= vec3(1.0, 1.0, 0.47);

const float far = 25000; // 1km

void main() {
    vec3 Nw = vec3(0, 1, 0);
    vec3 N = sampleNormal(fs_in.worldPos.xz);
    vec3 L = normalize(u.lightDirection.xyz);
    vec3 V = normalize(fs_in.viewDirection);

    float depth = 1 - linearizeDepth(gl_FragCoord.z, 1, far)/far;
    depth = pow(depth, u.normalFallOff);
    N = mix(Nw, N, depth);
    vec3 R = reflect(-V, N);

    vec3 env = vec3(1);
    vec3 turbulence;

    vec3 spec = specular(L, V, N);
//    vec3 spec = vec3(pow(clamp(dot(L, R), 0.0, 1.0), 400.0));
    vec3 diffuse = oceanColor[0]/3.1415926535;
    vec3 F = fresnel(R, N);
    vec3 radiance = (1 - F) * diffuse + spec * sunColor;

    vec3 color = mixWireFrame(radiance, distance);

    fragColor = vec4(color, 1);
    extras.x = gl_FragCoord.z;
}
