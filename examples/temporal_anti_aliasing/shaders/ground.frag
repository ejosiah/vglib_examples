#version 460

#include "lighting.glsl"
#include "pattern.glsl"

layout(push_constant) uniform UniformBufferObject{
    mat4 model;
    mat4 view;
    mat4 proj;
};

layout(location = 0) in struct {
    vec3 direction;
} fs_in;

layout(location = 0) out vec4 fragColor;


void main() {
    gl_FragDepth = 1;
    fragColor = vec4(0.2);
    vec3 o = (inverse(view) * vec4(0, 0, 0, 1)).xyz;

    vec3 n = vec3(0, 1, 0);
    vec3 rd = normalize(fs_in.direction);
    float t = -dot(n, o) / dot(n, rd);

    if(t > 0){
        vec3 p = o + rd * t;
        vec4 depth = proj * view * vec4(p, 1);
        depth /= depth.w;
        gl_FragDepth = depth.z;
        float sd = length(p.xz) - 10;
        sd = 1 - smoothstep(0, 10, sd);
        //      vec3 color = mod(floor(p.x) + floor(p.z), 2) == 0 ? vec3(1) : vec3(0.2);
        vec3 albedo =  GetColorFromPositionAndNormal(p, n);

        vec4 radiance = computeRadiance(createLightParams(p, o, n, defaultLightDir, 2, albedo));
        radiance.rgb = pow(vec3(1.0) - exp(-radiance.rgb / whitePoint * exposure), vec3(1.0 / 2.2));
        fragColor.rgb = mix(vec3(0.2), radiance.rgb, sd);
    }

}
