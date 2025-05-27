#version 460 core

#include "common.glsl"

vec3 GetColorFromPositionAndNormal( in vec3 worldPosition, in vec3 normal );

layout(location = 0) in struct {
    vec3 position;
    vec3 normal;
} fs_in;

layout(location = 0) out vec4 fracColor;


void main(){
    if(fs_in.normal.y > 0) discard;

    vec3 N = normalize(fs_in.normal);
    N = gl_FrontFacing ? N : -N;
    vec3 L = normalize(global.light - fs_in.position);
    vec3 color = GetColorFromPositionAndNormal(fs_in.position, fs_in.normal) * max(0, dot(N, L));

    fracColor = vec4(color, 1);
}

vec3 GetColorFromPositionAndNormal( in vec3 worldPosition, in vec3 normal ) {
    const float pi = 3.141519;
    float scale = 4;
    vec3 scaledPos = scale * worldPosition.xyz * pi * 2.0;
    vec3 scaledPos2 = scale * worldPosition.xyz * pi * 2.0 / 10.0 + vec3( pi / 4.0 );
    float s = cos( scaledPos2.x ) * cos( scaledPos2.y ) * cos( scaledPos2.z );  // [-1,1] range
    float t = cos( scaledPos.x ) * cos( scaledPos.y ) * cos( scaledPos.z );     // [-1,1] range


    t = ceil( t * 0.9 );
    s = ( ceil( s * 0.9 ) + 3.0 ) * 0.25;
    vec3 colorB = vec3( 0.85, 0.85, 0.85 );
    vec3 colorA = vec3( 1, 1, 1 );
    vec3 finalColor = mix( colorA, colorB, t ) * s;

    return vec3(0.8) * finalColor;
}