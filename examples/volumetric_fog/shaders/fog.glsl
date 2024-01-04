#ifndef FOG_GLSL
#define FOG_GLSL

#include "scene.glsl"


#ifndef FOG_SET
#define FOG_SET 0
#endif // FOG_SET

#define FOG_COLOR vec3(.9)

layout(set = FOG_SET, binding = 0) buffer FOG_INFO {
    ivec3 froxelDim;
    float scatteringFactor;

    float constantDensity;
    float heightFogDensity;
    float heightFogFalloff;
    float g;
} fog;

layout(set = FOG_SET, binding = 1) uniform sampler3D fogData;
layout(set = FOG_SET, binding = 2) uniform sampler3D lightContribution;
layout(set = FOG_SET, binding = 3) uniform sampler3D integratedScattering;

float linearToRawDepth(float depth, float near, float far) {
    return ( near * far ) / ( depth * ( near - far ) ) - far / ( near - far );
}

float sliceToExponentialDepth(float near, float far, int slice, int numSlices) {
    float x = (slice + .5)/float(numSlices);
    return near * pow(far/near, x);
}

vec2 uvFromFroxel(vec2 pos, ivec3 dim) {
    return pos / vec2(dim.x, dim.y);
}

vec3 worldPositionFromDepth(vec2 uv, float depth, mat4 inverseCamera) {
    vec4 ndc = vec4( uv * 2 - 1, depth, 1 );
    vec4 pos = inverseCamera * ndc;
    pos /= pos.w;

    return pos.xyz;
}

vec3 worldFromFroxel(vec3 fCoord) {
    vec2 uv = uvFromFroxel(fCoord.xy + vec2(.5), fog.froxelDim);
    int slice = int(fCoord.z);
    int numSlices = int(fog.froxelDim.z);

    float depth = sliceToExponentialDepth(scene.znear, scene.zfar, slice, numSlices);
    depth = linearToRawDepth(depth, scene.znear, scene.zfar);

    return worldPositionFromDepth(uv, depth, scene.inverseViewProjection);
}

vec4 scatteringExtinctionFromColorDensity(vec3 color, float density) {
    float extinction = fog.scatteringFactor * density;
    return vec4(color * extinction, extinction);
}

float phaseHG(float cos0, float g){
    const float PI = 3.14159265358979323846;
    float denom = 1 + g * g + 2 * g * cos0;
    return 1/(4 * PI) * (1 - g * g) / (denom * sqrt(denom));
}

float HG_p(float g, vec3 V, vec3 L){
    return phaseHG(dot(V, L), g);
}

#endif // FOG_GLSL