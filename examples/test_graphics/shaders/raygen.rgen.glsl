#version 460
#extension GL_EXT_ray_tracing : require

#include "ray_tracing_lang.glsl"

layout(set = 0, binding = 0) uniform accelerationStructure topLevelAs;
layout(set = 0, binding = 1) uniform CameraProperties{
    mat4 viewInverse;
    mat4 projInverse;
} cam;
layout(set = 0, binding = 2, rgba8) uniform image2D image;

layout(location = 0) rayPayload vec3 hitValue;

void main(){
    const vec2 pixelCenter = vec2(gl_LaunchID.xy) + vec2(0.5);
    const vec2 inUV = pixelCenter/vec2(gl_LaunchSize.xy);
    vec2 d = inUV * 2.0 - 1.0;

    vec4 origin = cam.viewInverse * vec4(0,0,0,1);
    vec4 target = cam.projInverse * vec4(d.x, d.y, 1, 1) ;
    vec4 direction = cam.viewInverse*vec4(normalize(target.xyz), 0) ;

    float tmin = 0.001;
    float tmax = 10000.0;

    hitValue = vec3(0.0);

    vec2 uv = vec2(gl_LaunchID.xy + uvec2(1))/vec2(gl_LaunchSize.xy);
    imageStore(image, ivec2(gl_LaunchID.xy), vec4(inUV, 0.0, 0.0));

}